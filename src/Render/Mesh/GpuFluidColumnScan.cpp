#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Gl/ComputeProgram.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <cstring>
#include <vector>

namespace cutum
{
namespace
{

uint64_t gFluidScanDispatches = 0;
uint64_t gFluidReadbacks = 0;
bool gPreferGpuFluidColumnScan = false;

struct FluidScanState
{
  UComputeProgram Program;
  GLuint FlagsSsbo{0};
  GLuint TopsSsbo{0};
  bool InitAttempted{false};
};

FluidScanState &ScanState()
{
  static FluidScanState s;
  return s;
}

bool EnsureFluidScanGpu()
{
  auto &s = ScanState();
  if (s.InitAttempted)
  {
    return s.Program.Valid();
  }
  s.InitAttempted = true;
  const RenderBackendCaps &caps = GetActiveRenderBackendCaps();
  if (!caps.HasCompute || !caps.HasSsbo)
  {
    return false;
  }
  // Prefer file shaders; fall back to inline GLES/desktop sources.
  if (!s.Program.CompileForCaps(caps, "shaders/compute/fluid_column_scan.comp",
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
    if (!s.Program.CompileSource(fallback))
    {
      return false;
    }
  }
  glGenBuffers(1, &s.FlagsSsbo);
  glGenBuffers(1, &s.TopsSsbo);
  return true;
}

bool TryGpuDispatchFluidColumns(const uint8_t *fluid_flags, int height,
                                std::vector<int16_t> &out_top_y)
{
  if (!EnsureFluidScanGpu())
  {
    return false;
  }
  auto &s = ScanState();
  const int n = CHUNK_SIZE;
  const size_t flag_bytes =
      static_cast<size_t>(height) * static_cast<size_t>(n * n);
  // Pack flags as uint bytes into SSBO (4 flags per uint).
  const size_t packed_words = (flag_bytes + 3) / 4;
  std::vector<uint32_t> packed(packed_words, 0);
  for (size_t i = 0; i < flag_bytes; ++i)
  {
    packed[i >> 2] |=
        (static_cast<uint32_t>(fluid_flags[i]) & 0xffu) << ((i & 3u) * 8u);
  }
  const size_t tops_count = static_cast<size_t>(n * n);
  out_top_y.assign(tops_count, static_cast<int16_t>(-1));

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.FlagsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(packed.size() * sizeof(uint32_t)),
               packed.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.TopsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)), nullptr,
               GL_DYNAMIC_READ);

  glUseProgram(s.Program.Program());
  glUniform1ui(glGetUniformLocation(s.Program.Program(), "side"),
               static_cast<GLuint>(n));
  glUniform1ui(glGetUniformLocation(s.Program.Program(), "height"),
               static_cast<GLuint>(height));
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.FlagsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.TopsSsbo);
  const GLuint groups = (static_cast<GLuint>(tops_count) + 63u) / 64u;
  glDispatchCompute(groups, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  std::vector<int32_t> tops32(tops_count);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.TopsSsbo);
  void *mapped =
      glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)),
                       GL_MAP_READ_BIT);
  if (mapped)
  {
    std::memcpy(tops32.data(), mapped, tops_count * sizeof(int32_t));
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  }
  else
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(tops_count * sizeof(int32_t)),
                       tops32.data());
#else
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return false;
#endif
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glUseProgram(0);

  for (size_t i = 0; i < tops_count; ++i)
  {
    out_top_y[i] = static_cast<int16_t>(tops32[i]);
  }
  ++gFluidScanDispatches;
  ++gFluidReadbacks;
  return true;
}

} // namespace

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

  // Desktop (GPF3): CPU scan on hot path — no sync GL readback on cruise.
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
