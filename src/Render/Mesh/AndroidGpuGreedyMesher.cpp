#include "Render/Mesh/AndroidGpuGreedyMesher.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Gl/ComputeProgram.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace cutum
{
namespace
{

struct AndroidMaskState
{
  UComputeProgram Program;
  GLuint OccSsbo{0};
  GLuint MaskSsbo{0};
  bool InitAttempted{false};
};

AndroidMaskState &MaskState()
{
  static AndroidMaskState s;
  return s;
}

const char *kFaceMaskGlesFallback = R"(#version 310 es
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Occ { uint occ[]; };
layout(std430, binding = 1) writeonly buffer Mask { uint mask[]; };
uniform uint volume;
uniform uint side;
uniform uint pad;
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
  int px = x + 1, py = y + 1, pz = z + 1;
  if (occAtPad(px, py, pz) == 0u) { mask[i] = 0u; return; }
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

bool EnsureAndroidMaskCompute()
{
  auto &s = MaskState();
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
  if (!s.Program.CompileForCaps(caps, "shaders/compute/face_mask_extract.comp",
                                "shaders/gles/compute/face_mask_extract.comp"))
  {
    if (caps.Platform == RenderPlatformKind::Android)
    {
      if (!s.Program.CompileSource(kFaceMaskGlesFallback))
      {
        return false;
      }
    }
    else
    {
      return false;
    }
  }
  glGenBuffers(1, &s.OccSsbo);
  glGenBuffers(1, &s.MaskSsbo);
  return s.Program.Valid() && s.OccSsbo != 0 && s.MaskSsbo != 0;
}

bool TryGpuFaceMaskExtract(const ChunkMeshSnapshot &snapshot,
                           UBlockRegistry &registry,
                           std::vector<uint32_t> &out_masks)
{
  if (!EnsureAndroidMaskCompute())
  {
    return false;
  }
  auto &s = MaskState();
  std::vector<uint8_t> occ;
  BuildPaddedOccupancy(snapshot, registry, occ);
  std::vector<uint32_t> occ_words((occ.size() + 3) / 4, 0);
  for (size_t i = 0; i < occ.size(); ++i)
  {
    occ_words[i >> 2] |= static_cast<uint32_t>(occ[i]) << ((i & 3u) * 8u);
  }
  const uint32_t volume = static_cast<uint32_t>(CHUNK_VOLUME);
  out_masks.assign(volume, 0);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.OccSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(occ_words.size() * sizeof(uint32_t)),
               occ_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.MaskSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(volume * sizeof(uint32_t)), nullptr,
               GL_DYNAMIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.OccSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.MaskSsbo);
  glUseProgram(s.Program.Program());
  glUniform1ui(glGetUniformLocation(s.Program.Program(), "volume"), volume);
  glUniform1ui(glGetUniformLocation(s.Program.Program(), "side"),
               static_cast<uint32_t>(CHUNK_SIZE));
  glUniform1ui(glGetUniformLocation(s.Program.Program(), "pad"),
               static_cast<uint32_t>(kGpuOccPad));
  glDispatchCompute((volume + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.MaskSsbo);
  void *mapped =
      glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(volume * sizeof(uint32_t)),
                       GL_MAP_READ_BIT);
  if (mapped)
  {
    std::memcpy(out_masks.data(), mapped, volume * sizeof(uint32_t));
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  }
  else
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(volume * sizeof(uint32_t)),
                       out_masks.data());
#else
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glUseProgram(0);
    return false;
#endif
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glUseProgram(0);
  // Pack-time / deferred main-thread readback only — not cruise hot-path
  // gpu_mask_readback telemetry (UGpuGreedyMesher counter stays 0).
  return true;
}

bool QuadsToBatches(const std::vector<GreedyQuad> &quads,
                    UBlockRegistry &registry, glm::ivec3 coord,
                    std::vector<GreedyMeshBatch> &out_batches)
{
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

} // namespace

UAndroidGpuGreedyMesher::UAndroidGpuGreedyMesher() = default;
UAndroidGpuGreedyMesher::~UAndroidGpuGreedyMesher() = default;

std::vector<GreedyQuad>
UAndroidGpuGreedyMesher::BuildChunkMesh(const UBlockWorld &world,
                                        glm::ivec3 chunk_coord,
                                        UBlockRegistry &registry)
{
  return Cpu.BuildChunkMesh(world, chunk_coord, registry);
}

std::vector<GreedyQuad>
UAndroidGpuGreedyMesher::BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                        UBlockRegistry &registry)
{
  return Cpu.BuildChunkMesh(snapshot, registry);
}

bool UAndroidGpuGreedyMesher::CanDeferGpuExtract(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry) const
{
  const RenderBackendCaps &caps = GetActiveRenderBackendCaps();
  if (!caps.AllowAndroidGpu)
  {
    return false;
  }
  return SnapshotIsGpuExtractEligible(snapshot, registry);
}

bool UAndroidGpuGreedyMesher::TryExtractOpaqueToBatches(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
    glm::ivec3 coord, std::vector<GreedyMeshBatch> &out_batches,
    bool deferred_no_gpu_readback, bool /*greedy_merge_rects*/)
{
  out_batches.clear();
  if (!SnapshotIsGpuExtractEligible(snapshot, registry))
  {
    return false;
  }

  std::vector<GreedyQuad> quads;
  if (!deferred_no_gpu_readback)
  {
    // Main-thread: GLES/GL compute mask → decode → strict merge.
    std::vector<uint32_t> masks;
    if (TryGpuFaceMaskExtract(snapshot, registry, masks))
    {
      quads = MergeOpaqueQuadsStrict(DecodeFaceMasks(snapshot, registry, masks));
    }
  }
  if (quads.empty())
  {
    // Worker / compute-fail fallback: CPU extract (no GL).
    quads = MergeOpaqueQuadsStrict(ExtractOpaqueFacesCpu(snapshot, registry));
  }
  return QuadsToBatches(quads, registry, coord, out_batches);
}

} // namespace cutum
