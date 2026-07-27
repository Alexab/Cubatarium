#include "Render/Mesh/GpuGreedyMesher.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cutum
{
namespace
{

uint64_t gMeshVboDispatches = 0;
uint64_t gMaskReadbacks = 0;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
// Padded occupancy → FaceMask[CHUNK_VOLUME] (6 bits: -X+X-Y+Y-Z+Z).
// occ layout: side=CHUNK_SIZE+2; interior voxel (x,y,z) at
// ((y+1)*side+(z+1))*side+(x+1).
const char *kFaceExtractCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Occ { uint occ[]; }; // packed 4 bytes
layout(std430, binding = 1) writeonly buffer Mask { uint mask[]; };
uniform uint volume; // CHUNK_VOLUME
uniform uint side;   // CHUNK_SIZE
uniform uint pad;    // CHUNK_SIZE+2

uint occAtPad(int px, int py, int pz) {
  uint i = uint((py * int(pad) + pz) * int(pad) + px);
  uint word = occ[i >> 2];
  return (word >> ((i & 3u) * 8u)) & 0xffu;
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= volume) return;
  int side_i = int(side);
  int x = int(i % uint(side_i));
  int y = int(i / uint(side_i * side_i));
  int z = int((i / uint(side_i)) % uint(side_i));
  int px = x + 1;
  int py = y + 1;
  int pz = z + 1;
  if (occAtPad(px, py, pz) == 0u) {
    mask[i] = 0u;
    return;
  }
  uint m = 0u;
  if (occAtPad(px - 1, py, pz) == 0u) m |= 1u;
  if (occAtPad(px + 1, py, pz) == 0u) m |= 2u;
  if (occAtPad(px, py - 1, pz) == 0u) m |= 4u;
  if (occAtPad(px, py + 1, pz) == 0u) m |= 8u;
  if (occAtPad(px, py, pz - 1) == 0u) m |= 16u;
  if (occAtPad(px, py, pz + 1) == 0u) m |= 32u;
  mask[i] = m;
}
)";
#endif

std::vector<GreedyQuad>
DecodeFaceMasks(const ChunkMeshSnapshot &snap, UBlockRegistry &registry,
                const std::vector<uint32_t> &masks)
{
  (void)registry;
  std::vector<GreedyQuad> quads;
  const int n = CHUNK_SIZE;
  const size_t vol = static_cast<size_t>(CHUNK_VOLUME);
  for (size_t i = 0; i < vol && i < masks.size(); ++i)
  {
    const uint32_t m = masks[i];
    if (m == 0)
    {
      continue;
    }
    const int x = static_cast<int>(i % n);
    const int y = static_cast<int>(i / (n * n));
    const int z = static_cast<int>((i / n) % n);
    const BlockId id = snap.blocks[i];
    auto emit = [&](int axis, int sign, uint32_t bit)
    {
      if ((m & bit) == 0)
      {
        return;
      }
      GreedyQuad q;
      q.axis = axis;
      q.slice = (axis == 0 ? x : (axis == 1 ? y : z)) + (sign > 0 ? 1 : 0);
      if (axis == 0)
      {
        q.u = z;
        q.v = y;
      }
      else if (axis == 1)
      {
        q.u = x;
        q.v = z;
      }
      else
      {
        q.u = x;
        q.v = y;
      }
      q.width = 1;
      q.height = 1;
      q.Id = id;
      q.faceSign = sign;
      q.LightPacked = snap.GetLightPackedLocal(glm::ivec3(x, y, z));
      quads.push_back(q);
    };
    emit(0, -1, 1u);
    emit(0, 1, 2u);
    emit(1, -1, 4u);
    emit(1, 1, 8u);
    emit(2, -1, 16u);
    emit(2, 1, 32u);
  }
  return quads;
}

} // namespace

struct UGpuGreedyMesher::GpuState
{
  GLuint Program{0};
  GLuint OccSsbo{0};
  GLuint MaskSsbo{0};
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
  if (State->OccSsbo)
  {
    glDeleteBuffers(1, &State->OccSsbo);
  }
  if (State->MaskSsbo)
  {
    glDeleteBuffers(1, &State->MaskSsbo);
  }
#endif
}

bool UGpuGreedyMesher::EnsureCompute()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return false;
#else
  if (State->InitAttempted)
  {
    return State->Program != 0;
  }
  State->InitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kFaceExtractCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuGreedyMesher] face extract compile failed: " << log;
    glDeleteShader(sh);
    return false;
  }
  State->Program = glCreateProgram();
  glAttachShader(State->Program, sh);
  glLinkProgram(State->Program);
  glDeleteShader(sh);
  glGetProgramiv(State->Program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[GpuGreedyMesher] face extract link failed";
    glDeleteProgram(State->Program);
    State->Program = 0;
    return false;
  }
  glGenBuffers(1, &State->OccSsbo);
  glGenBuffers(1, &State->MaskSsbo);
  return true;
#endif
}

std::vector<GreedyQuad>
UGpuGreedyMesher::TryComputeExtract(const ChunkMeshSnapshot &snapshot,
                                    UBlockRegistry &registry)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)snapshot;
  (void)registry;
  return {};
#else
  // Async mesh workers have no GL context — never issue GL from them.
#if defined(_WIN32)
  if (wglGetCurrentContext() == nullptr)
  {
    return {};
  }
#endif
  if (!SnapshotIsGpuExtractEligible(snapshot, registry))
  {
    return {};
  }
  // D1.1: no mask GetBufferSubData on hot path. Face extract on CPU (1x1);
  // MergeOpaqueQuadsStrict available for parity tests / legacy decode. Full
  // greedy rectangles stay on Cpu.BuildChunkMesh fallback for mixed chunks.
  const char *legacy = std::getenv("CUBATARIUM_GPU_MASK_READBACK");
  if (legacy && legacy[0] == '1' && EnsureCompute())
  {
    std::vector<uint8_t> occ;
    BuildPaddedOccupancy(snapshot, registry, occ);
    std::vector<uint32_t> occ_words((occ.size() + 3) / 4, 0);
    for (size_t i = 0; i < occ.size(); ++i)
    {
      occ_words[i >> 2] |= static_cast<uint32_t>(occ[i]) << ((i & 3u) * 8u);
    }
    const uint32_t volume = static_cast<uint32_t>(CHUNK_VOLUME);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->OccSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(occ_words.size() * sizeof(uint32_t)),
                 occ_words.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->MaskSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(volume * sizeof(uint32_t)), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, State->OccSsbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, State->MaskSsbo);
    glUseProgram(State->Program);
    glUniform1ui(glGetUniformLocation(State->Program, "volume"), volume);
    glUniform1ui(glGetUniformLocation(State->Program, "side"),
                 static_cast<uint32_t>(CHUNK_SIZE));
    glUniform1ui(glGetUniformLocation(State->Program, "pad"),
                 static_cast<uint32_t>(kGpuOccPad));
    glDispatchCompute((volume + 63u) / 64u, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glUseProgram(0);
    std::vector<uint32_t> masks(volume, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, State->MaskSsbo);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(masks.size() * sizeof(uint32_t)),
                       masks.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    ++ComputeDispatches;
    ++gMeshVboDispatches;
    ++gMaskReadbacks;
    return MergeOpaqueQuadsStrict(DecodeFaceMasks(snapshot, registry, masks));
  }

  ++ComputeDispatches;
  ++gMeshVboDispatches;
  return ExtractOpaqueFacesCpu(snapshot, registry);
#endif
}

bool UGpuGreedyMesher::CanDeferGpuExtract(const ChunkMeshSnapshot &snapshot,
                                          UBlockRegistry &registry) const
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)snapshot;
  (void)registry;
  return false;
#else
  return SnapshotIsGpuExtractEligible(snapshot, registry);
#endif
}

bool UGpuGreedyMesher::TryExtractOpaqueToBatches(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
    glm::ivec3 coord, std::vector<GreedyMeshBatch> &out_batches,
    bool deferred_no_gpu_readback)
{
  out_batches.clear();
  std::vector<GreedyQuad> quads;
  if (deferred_no_gpu_readback)
  {
    if (!SnapshotIsGpuExtractEligible(snapshot, registry))
    {
      return false;
    }
    // Same as TryComputeExtract default: CPU face extract, no mask readback.
    quads = ExtractOpaqueFacesCpu(snapshot, registry);
  }
  else
  {
    quads = TryComputeExtract(snapshot, registry);
  }
  if (quads.empty())
  {
    return false;
  }
  std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
  for (const GreedyQuad &q : quads)
  {
    GreedyMeshBatch &batch = byBlockId[q.Id];
    batch.blockId = q.Id;
    batch.Transparent = registry.IsTransparent(q.Id);
    batch.AlphaCutout =
        registry.GetRenderStyle(q.Id) == BlockRenderStyle::Cutout;
    const size_t base_vertex = batch.vertices.size();
    AppendGreedyQuad(q, coord, batch.vertices, batch.indices);
    for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
    {
      ApplyVertexLight(batch.vertices[i], q.LightPacked);
    }
  }
  out_batches.reserve(byBlockId.size());
  for (auto &entry : byBlockId)
  {
    entry.second.blockId = entry.first;
    out_batches.push_back(std::move(entry.second));
  }
  return !out_batches.empty();
}

uint64_t UGpuGreedyMesher::ConsumeMeshVboDispatchCount()
{
  const uint64_t v = gMeshVboDispatches;
  gMeshVboDispatches = 0;
  return v;
}

uint64_t UGpuGreedyMesher::ConsumeMaskReadbackCount()
{
  const uint64_t v = gMaskReadbacks;
  gMaskReadbacks = 0;
  return v;
}

std::vector<GreedyQuad>
UGpuGreedyMesher::BuildChunkMesh(const UBlockWorld &world,
                                 glm::ivec3 chunk_coord,
                                 UBlockRegistry &registry)
{
  const ChunkMeshSnapshot snap =
      ChunkMeshSnapshot::Capture(world, chunk_coord, /*rev*/ 0);
  auto gpu = TryComputeExtract(snap, registry);
  if (!gpu.empty())
  {
    return gpu;
  }
  return Cpu.BuildChunkMesh(world, chunk_coord, registry);
}

std::vector<GreedyQuad>
UGpuGreedyMesher::BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                 UBlockRegistry &registry)
{
  auto gpu = TryComputeExtract(snapshot, registry);
  if (!gpu.empty())
  {
    return gpu;
  }
  return Cpu.BuildChunkMesh(snapshot, registry);
}

} // namespace cutum
