#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Gl/ComputeBufferBindings.h"
#include "Render/Gl/ComputeScratchBuffers.h"
#include <cstring>
#include <limits>
#include <vector>

namespace cutum
{
namespace
{

uint64_t gFluidScanDispatches = 0;
uint64_t gFluidReadbacks = 0;
bool gPreferGpuFluidColumnScan = false;

ComputeScratchBuffers<2> &ScanState()
{
  static thread_local ComputeScratchBuffers<2> s;
  return s;
}

bool CompileFluidScan(UComputeProgram &program, const RenderBackendCaps &caps)
{
  // Prefer file shaders; fall back to inline GLES/desktop sources.
  if (!program.CompileForCaps(caps, "shaders/compute/fluid_column_scan.comp",
                              "shaders/gles/compute/fluid_column_scan.comp"))
  {
    const char *fallback =
        caps.Platform == RenderPlatformKind::Android
            ? R"(#version 310 es
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Flags { uint flags[]; };
layout(std430, binding = 1) writeonly buffer Tops { int tops[]; };
uniform uint side;
uniform uint height;
uint flagAt(int x, int y, int z) {
  uint i = uint((y * int(side) + z) * int(side) + x);
  uint word = flags[i >> 2];
  return (word >> ((i & 3u) * 8u)) & 0xffu;
}
void main() {
  uint col = gl_GlobalInvocationID.x;
  uint n2 = side * side;
  if (col >= n2) return;
  int x = int(col % side);
  int z = int(col / side);
  int top = -1;
  for (int y = 0; y < int(height); ++y) {
    if (flagAt(x, y, z) != 0u) top = y;
  }
  tops[col] = top;
}
)"
            : R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Flags { uint flags[]; };
layout(std430, binding = 1) writeonly buffer Tops { int tops[]; };
uniform uint side;
uniform uint height;
uint flagAt(int x, int y, int z) {
  uint i = uint((y * int(side) + z) * int(side) + x);
  uint word = flags[i >> 2];
  return (word >> ((i & 3u) * 8u)) & 0xffu;
}
void main() {
  uint col = gl_GlobalInvocationID.x;
  uint n2 = side * side;
  if (col >= n2) return;
  int x = int(col % side);
  int z = int(col / side);
  int top = -1;
  for (int y = 0; y < int(height); ++y) {
    if (flagAt(x, y, z) != 0u) top = y;
  }
  tops[col] = top;
}
)";
    return program.CompileSource(fallback);
  }
  return true;
}
} // namespace

void InspectFluidColumnScanGpuBuffers(TerrainGpuBufferCensus &out)
{
  ScanState().Inspect(out, TerrainGpuBufferRole::FluidColumnScan);
}

void ReleaseFluidColumnScanGpuStorage(const void *context)
{
  ScanState().Release(context ? context : CurrentComputeContext());
}

bool TryGpuDispatchFluidColumns(const uint8_t *fluid_flags, int height,
                                std::vector<int16_t> &out_top_y)
{
  if (!fluid_flags || height <= 0 || height > std::numeric_limits<int16_t>::max() + 1 || !ComputeStorageAvailable())
    return false;
  const auto &caps = GetActiveRenderBackendCaps();
  if (!caps.HasCompute || !caps.HasSsbo)
    return false;
  constexpr int n = CHUNK_SIZE;
  constexpr size_t tops_count = n * n;
  const size_t flag_bytes = static_cast<size_t>(height) * tops_count;
  // Pack flags as uint bytes into SSBO (4 flags per uint).
  const size_t packed_words = (flag_bytes + 3) / 4;
  std::vector<uint32_t> packed(packed_words, 0);
  for (size_t i = 0; i < flag_bytes; ++i)
  {
    packed[i >> 2] |=
        (static_cast<uint32_t>(fluid_flags[i]) & 0xffu) << ((i & 3u) * 8u);
  }
  std::vector<int32_t> tops32(tops_count);
  std::vector<int16_t> next(tops_count);
  auto &s = ScanState();
  if (!s.Initialize([&](UComputeProgram &program) { return CompileFluidScan(program, caps); }))
    return false;
  const ComputeBufferBindings<2> restore;
  const auto [flagsSsbo, topsSsbo] = s.names;

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, flagsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(packed.size() * sizeof(uint32_t)),
               packed.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, topsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)), nullptr,
               GL_DYNAMIC_READ);
  if (glGetError() != GL_NO_ERROR)
  {
    s.DestroyBuffers();
    return false;
  }

  glUseProgram(s.program);
  glUniform1ui(glGetUniformLocation(s.program, "side"),
               static_cast<GLuint>(n));
  glUniform1ui(glGetUniformLocation(s.program, "height"),
               static_cast<GLuint>(height));
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, flagsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, topsSsbo);
  const GLuint groups = (static_cast<GLuint>(tops_count) + 63u) / 64u;
  glDispatchCompute(groups, 1, 1);
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  if (glGetError() != GL_NO_ERROR)
    return false;

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, topsSsbo);
  void *mapped =
      glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)),
                       GL_MAP_READ_BIT);
  if (mapped)
  {
    std::memcpy(tops32.data(), mapped, tops_count * sizeof(int32_t));
    if (glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) != GL_TRUE)
      return false;
  }
  else
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    if (glGetError() != GL_NO_ERROR)
      return false;
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)),
                       tops32.data());
#else
    return false;
#endif
  }
  if (glGetError() != GL_NO_ERROR)
    return false;

  for (size_t i = 0; i < tops_count; ++i)
  {
    if (tops32[i] < -1 || tops32[i] >= height)
      return false;
    next[i] = static_cast<int16_t>(tops32[i]);
  }
  out_top_y.swap(next);
  ++gFluidScanDispatches;
  ++gFluidReadbacks;
  return true;
}

void SetPreferGpuFluidColumnScan(bool prefer)
{
  gPreferGpuFluidColumnScan = prefer;
}

bool PreferGpuFluidColumnScan() { return gPreferGpuFluidColumnScan; }

uint64_t GpuFluidColumnScanDispatchCount() { return gFluidScanDispatches; }

uint64_t ConsumeGpuFluidReadbackCount()
{
  const uint64_t v = gFluidReadbacks;
  gFluidReadbacks = 0;
  return v;
}

bool TryGpuScanFluidColumns(const uint8_t *fluid_flags, int height,
                            std::vector<int16_t> &out_top_y)
{
  if (!fluid_flags || height <= 0)
  {
    return false;
  }
  const RenderBackendCaps &caps = GetActiveRenderBackendCaps();

  // Desktop (GPF3): intentional CPU column scan on the hot path — NOT a GPU
  // compute dispatch. Naming retained as TryGpu* for API stability; this branch
  // avoids sync GL readback hitch on cruise. Android uses real GLES compute.
  if (caps.Platform == RenderPlatformKind::Desktop)
  {
    ScanFluidColumnsCpu(fluid_flags, height, out_top_y);
    ++gFluidScanDispatches;
    return true;
  }

  // Android: real GLES compute only when effective GPU is allowed.
  if (!caps.AllowAndroidGpu || !caps.HasCompute || !caps.HasSsbo)
  {
    return false;
  }
  return TryGpuDispatchFluidColumns(fluid_flags, height, out_top_y);
}

} // namespace cutum
