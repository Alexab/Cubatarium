#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <vector>

namespace cutum
{
namespace
{

uint64_t gFluidScanDispatches = 0;
uint64_t gFluidReadbacks = 0;
bool gPreferGpuFluidColumnScan = false;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
const char *kFluidColumnScanCompute = R"(#version 430
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
    if (flagAt(x, y, z) != 0u) {
      top = y;
    }
  }
  tops[col] = top;
}
)";

struct FluidScanState
{
  GLuint Program{0};
  GLuint FlagsSsbo{0};
  GLuint TopsSsbo{0};
  bool InitAttempted{false};
};

FluidScanState &ScanState()
{
  static FluidScanState s;
  return s;
}

bool EnsureFluidScan()
{
  auto &s = ScanState();
  if (s.InitAttempted)
  {
    return s.Program != 0;
  }
  s.InitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kFluidColumnScanCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuFluidColumnScan] compile failed: " << log;
    glDeleteShader(sh);
    return false;
  }
  s.Program = glCreateProgram();
  glAttachShader(s.Program, sh);
  glLinkProgram(s.Program);
  glDeleteShader(sh);
  glGetProgramiv(s.Program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    glDeleteProgram(s.Program);
    s.Program = 0;
    return false;
  }
  glGenBuffers(1, &s.FlagsSsbo);
  glGenBuffers(1, &s.TopsSsbo);
  return true;
}
#endif

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
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)fluid_flags;
  (void)height;
  (void)out_top_y;
  return false;
#else
  if (!fluid_flags || height <= 0)
  {
    return false;
  }
  // GPF3: drop sync GL readback on hot path; scan prebuilt flags on CPU.
  // This keeps the P7 reuse/cache flow intact without GL stalls.
  ScanFluidColumnsCpu(fluid_flags, height, out_top_y);
  ++gFluidScanDispatches;
  return true;
#endif
}

} // namespace cutum
