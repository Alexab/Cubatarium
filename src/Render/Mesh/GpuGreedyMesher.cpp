#include "Render/Mesh/GpuGreedyMesher.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"

namespace cutum
{
namespace
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
const char *kVoxelCountCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Counter { uint count; };
void main() {
  if (gl_GlobalInvocationID.x == 0u) {
    atomicAdd(count, 1u);
  }
}
)";
#endif
} // namespace

struct UGpuGreedyMesher::GpuState
{
  GLuint Program{0};
  GLuint CounterSsbo{0};
  bool InitAttempted{false};
};

UGpuGreedyMesher::UGpuGreedyMesher() : State(std::make_unique<GpuState>()) {}

UGpuGreedyMesher::~UGpuGreedyMesher()
{
  if (!State)
  {
    return;
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (State->Program)
  {
    glDeleteProgram(State->Program);
  }
  if (State->CounterSsbo)
  {
    glDeleteBuffers(1, &State->CounterSsbo);
  }
#endif
}

void UGpuGreedyMesher::WarmCompute()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return;
#else
  if (!State || State->InitAttempted)
  {
    return;
  }
  State->InitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kVoxelCountCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[GpuGreedyMesher] compute compile failed";
    glDeleteShader(sh);
    return;
  }
  State->Program = glCreateProgram();
  glAttachShader(State->Program, sh);
  glLinkProgram(State->Program);
  glDeleteShader(sh);
  glGetProgramiv(State->Program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[GpuGreedyMesher] compute link failed";
    glDeleteProgram(State->Program);
    State->Program = 0;
    return;
  }
  glGenBuffers(1, &State->CounterSsbo);
  uint32_t zero = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->CounterSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), &zero,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, State->CounterSsbo);
  glUseProgram(State->Program);
  glDispatchCompute(1, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);
  ++ComputeDispatches;
#endif
}

std::vector<GreedyQuad>
UGpuGreedyMesher::BuildChunkMesh(const UBlockWorld &world,
                                 glm::ivec3 chunk_coord,
                                 UBlockRegistry &registry)
{
  WarmCompute();
  return Cpu.BuildChunkMesh(world, chunk_coord, registry);
}

std::vector<GreedyQuad>
UGpuGreedyMesher::BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                 UBlockRegistry &registry)
{
  WarmCompute();
  return Cpu.BuildChunkMesh(snapshot, registry);
}

} // namespace cutum
