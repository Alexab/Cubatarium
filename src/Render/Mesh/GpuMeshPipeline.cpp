#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/PackedQuad.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <algorithm>
#include <array>

namespace cutum
{
namespace
{

void BuildBlockRangesFromQuads(const std::vector<PackedQuad> &quads,
                               UBlockRegistry &registry,
                               std::vector<GpuBlockDrawRange> &out_ranges)
{
  out_ranges.clear();
  if (quads.empty())
  {
    return;
  }
  size_t i = 0;
  while (i < quads.size())
  {
    const BlockId bid = static_cast<BlockId>(quads[i].BlockType());
    size_t j = i + 1;
    while (j < quads.size() &&
           static_cast<BlockId>(quads[j].BlockType()) == bid)
    {
      ++j;
    }
    GpuBlockDrawRange range;
    range.blockId = bid;
    range.quadOffset = static_cast<uint32_t>(i);
    range.quadCount = static_cast<uint32_t>(j - i);
    range.Transparent = registry.IsTransparent(bid);
    range.AlphaCutout =
        registry.GetRenderStyle(bid) == BlockRenderStyle::Cutout;
    out_ranges.push_back(range);
    i = j;
  }
}

bool ChunkHasTransparentOrCutout(const ChunkMeshSnapshot &snapshot,
                                 UBlockRegistry &registry)
{
  for (BlockId id : snapshot.blocks)
  {
    if (id != 0 && (registry.IsTransparent(id) ||
                    registry.GetRenderStyle(id) == BlockRenderStyle::Cutout))
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool UGpuMeshPipeline::Init(uint32_t max_slots)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)max_slots;
  return false;
#else
  if (!EnsureGpuOpaqueEmit(EmitState) || EmitState.PackedEmitProgram == 0)
  {
    LOG(WARNING) << "[GpuMeshPipeline] compute programs failed to compile";
    return false;
  }
  if (!Allocator.Init(max_slots))
  {
    LOG(WARNING) << "[GpuMeshPipeline] slot allocator init failed";
    return false;
  }
  Ready = true;
  LOG(INFO) << "[GpuMeshPipeline] initialized with " << max_slots << " slots";
  return true;
#endif
}

void UGpuMeshPipeline::Shutdown()
{
  Allocator.Shutdown();
  while (!PendingQueue.empty())
  {
    PendingQueue.pop();
  }
  Ready = false;
}

void UGpuMeshPipeline::EnqueueSnapshot(ChunkMeshSnapshot snapshot,
                                       uint64_t source_revision)
{
  PendingQueue.push({std::move(snapshot), source_revision});
}

bool UGpuMeshPipeline::RunComputePasses(const ChunkMeshSnapshot &snapshot,
                                        UBlockRegistry &registry,
                                        glm::ivec3 coord, int slot_idx,
                                        uint32_t &out_quad_count,
                                        std::vector<PackedQuad> *out_sorted_quads)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)snapshot;
  (void)registry;
  (void)coord;
  (void)slot_idx;
  (void)out_sorted_quads;
  out_quad_count = 0;
  return false;
#else
  out_quad_count = 0;
  if (out_sorted_quads)
  {
    out_sorted_quads->clear();
  }
  if (!SnapshotIsGpuExtractEligible(snapshot, registry) ||
      EmitState.PackedEmitProgram == 0)
  {
    return false;
  }

  std::vector<uint8_t> occ;
  BuildPaddedOccupancy(snapshot, registry, occ);
  std::vector<uint32_t> occ_words;
  occ_words.assign((occ.size() + 3) / 4, 0);
  for (size_t i = 0; i < occ.size(); ++i)
  {
    occ_words[i >> 2] |= static_cast<uint32_t>(occ[i]) << ((i & 3u) * 8u);
  }

  std::array<uint8_t, CHUNK_VOLUME> blocks{};
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    blocks[static_cast<size_t>(i)] =
        static_cast<uint8_t>(snapshot.blocks[static_cast<size_t>(i)]);
  }
  std::vector<uint8_t> padded_lights;
  BuildPaddedLight(snapshot, padded_lights);
  std::vector<uint32_t> block_words;
  std::vector<uint32_t> light_words;
  block_words.assign((blocks.size() + 3) / 4, 0);
  for (size_t i = 0; i < blocks.size(); ++i)
  {
    block_words[i >> 2] |= static_cast<uint32_t>(blocks[i]) << ((i & 3u) * 8u);
  }
  light_words.assign((padded_lights.size() + 3) / 4, 0);
  for (size_t i = 0; i < padded_lights.size(); ++i)
  {
    light_words[i >> 2] |=
        static_cast<uint32_t>(padded_lights[i]) << ((i & 3u) * 8u);
  }

  const uint32_t volume = static_cast<uint32_t>(CHUNK_VOLUME);
  const uint32_t side = static_cast<uint32_t>(CHUNK_SIZE);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.OccSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(occ_words.size() * sizeof(uint32_t)),
               occ_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.MaskSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(volume * sizeof(uint32_t)), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.BlocksSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(block_words.size() * sizeof(uint32_t)),
               block_words.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.LightsSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               static_cast<GLsizeiptr>(light_words.size() * sizeof(uint32_t)),
               light_words.data(), GL_DYNAMIC_DRAW);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, EmitState.OccSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, EmitState.MaskSsbo);
  glUseProgram(EmitState.MaskProgram);
  glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "volume"), volume);
  glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "side"), side);
  glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "pad"),
               static_cast<uint32_t>(kGpuOccPad));
  glDispatchCompute((volume + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  const std::array<uint32_t, 4> zero_counters{0, 0, 0, 0};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.CountersSsbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero_counters),
               zero_counters.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, EmitState.MaskSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, EmitState.BlocksSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, EmitState.LightsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, EmitState.RectsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, EmitState.CountersSsbo);
  glUseProgram(EmitState.GreedyProgram);
  glUniform1ui(glGetUniformLocation(EmitState.GreedyProgram, "side"), side);
  glUniform1ui(glGetUniformLocation(EmitState.GreedyProgram, "pad"),
               static_cast<uint32_t>(kGpuOccPad));
  glDispatchCompute(102u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // Counter readback is 16B — required for emit sizing. Full-quad download
  // happens once below; ProcessSnapshot reuses it (Phase C: was duplicated).
  std::array<uint32_t, 4> counters{};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.CountersSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters),
                     counters.data());
  const uint32_t rect_count = counters[0];
  if (rect_count == 0)
  {
    glUseProgram(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    Allocator.SetSlotQuadCount(slot_idx, 0);
    return true;
  }
  if (rect_count > UGpuMeshSlotAllocator::kMaxQuadsPerSlot)
  {
    LOG(WARNING) << "[GpuMeshPipeline] rect overflow " << rect_count;
    glUseProgram(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return false;
  }

  const GpuMeshSlot *slot = Allocator.GetSlot(coord);
  if (!slot)
  {
    return false;
  }
  const uint32_t slot_offset = slot->OffsetQuads;
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, EmitState.RectsSsbo);
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, Allocator.GetQuadSsbo(),
                    static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
                    static_cast<GLsizeiptr>(rect_count * sizeof(PackedQuad)));
  glUseProgram(EmitState.PackedEmitProgram);
  glUniform1ui(glGetUniformLocation(EmitState.PackedEmitProgram, "numRects"),
               rect_count);
  glDispatchCompute((rect_count + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::vector<PackedQuad> quads(rect_count);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, Allocator.GetQuadSsbo());
  glGetBufferSubData(
      GL_SHADER_STORAGE_BUFFER,
      static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
      static_cast<GLsizeiptr>(quads.size() * sizeof(PackedQuad)), quads.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::stable_sort(quads.begin(), quads.end(),
                   [](const PackedQuad &a, const PackedQuad &b)
                   { return a.BlockType() < b.BlockType(); });
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, Allocator.GetQuadSsbo());
  glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                  static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
                  static_cast<GLsizeiptr>(quads.size() * sizeof(PackedQuad)),
                  quads.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  Allocator.SetSlotQuadCount(slot_idx, rect_count);
  out_quad_count = rect_count;
  if (out_sorted_quads)
  {
    *out_sorted_quads = std::move(quads);
  }
  return true;
#endif
}

bool UGpuMeshPipeline::ProcessSnapshot(const ChunkMeshSnapshot &snapshot,
                                       UBlockRegistry &registry,
                                       GpuMeshProcessResult &out_result)
{
  out_result = {};
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)snapshot;
  (void)registry;
  return false;
#else
  if (!Ready)
  {
    return false;
  }
  if (!SnapshotIsGpuExtractEligible(snapshot, registry))
  {
    return false;
  }

  const glm::ivec3 coord = snapshot.coord;
  const bool has_transparent = ChunkHasTransparentOrCutout(snapshot, registry);
  const int slot_idx = Allocator.AllocateSlot(coord, has_transparent);
  if (slot_idx < 0)
  {
    return false;
  }

  uint32_t quad_count = 0;
  std::vector<PackedQuad> quads;
  if (!RunComputePasses(snapshot, registry, coord, slot_idx, quad_count,
                        &quads))
  {
    Allocator.FreeSlot(coord);
    return false;
  }

  if (quad_count == 0)
  {
    out_result.success = true;
    out_result.slotIndex = slot_idx;
    out_result.quadCount = 0;
    out_result.transparent = has_transparent;
    return true;
  }

  // Ranges/dark from the same CPU buffer used for sort — no second SSBO download.
  BuildBlockRangesFromQuads(quads, registry, out_result.blockRanges);
  out_result.hasFullyDarkFace = PackedQuadsHaveFullyDarkFace(quads);

  out_result.success = true;
  out_result.slotIndex = slot_idx;
  out_result.quadCount = quad_count;
  out_result.transparent = has_transparent;
  return true;
#endif
}

int UGpuMeshPipeline::ProcessQueue(UBlockRegistry &registry, int budget)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)registry;
  (void)budget;
  return 0;
#else
  if (!Ready || PendingQueue.empty())
  {
    return 0;
  }

  int processed = 0;
  while (!PendingQueue.empty() && processed < budget)
  {
    PendingChunk pending = std::move(PendingQueue.front());
    PendingQueue.pop();
    GpuMeshProcessResult result;
    if (ProcessSnapshot(pending.Snapshot, registry, result))
    {
      ++processed;
    }
  }
  return processed;
#endif
}

void UGpuMeshPipeline::FreeChunk(glm::ivec3 chunk_coord)
{
  Allocator.FreeSlot(chunk_coord);
}

bool UGpuMeshPipeline::HasGpuMesh(glm::ivec3 chunk_coord) const
{
  if (!Ready)
  {
    return false;
  }
  const GpuMeshSlot *slot = Allocator.GetSlot(chunk_coord);
  return slot && slot->QuadCount > 0;
}

} // namespace cutum
