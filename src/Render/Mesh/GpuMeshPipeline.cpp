#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/PackedQuad.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"
#include <array>

namespace cutum
{
namespace
{

constexpr size_t kBlockTypeBuckets = 1024;
constexpr size_t kSortCountsWords = kBlockTypeBuckets + 1; // + dark flag

/// BlockType is 10 bits → 1024 buckets. Stable counting sort (CPU fallback).
void CountingSortPackedQuadsByBlockType(std::vector<PackedQuad> &quads,
                                        std::vector<PackedQuad> &scratch,
                                        UBlockRegistry &registry,
                                        std::vector<GpuBlockDrawRange> *out_ranges,
                                        bool *out_has_dark)
{
  std::array<uint32_t, kBlockTypeBuckets> counts{};
  bool has_dark = false;
  for (const PackedQuad &q : quads)
  {
    ++counts[static_cast<size_t>(q.BlockType())];
    if (!has_dark && q.Face() != 5 && q.SkyLight() <= 0 &&
        q.BlockLight() <= 0)
    {
      has_dark = true;
    }
  }
  std::array<uint32_t, kBlockTypeBuckets> offsets{};
  uint32_t total = 0;
  for (size_t b = 0; b < kBlockTypeBuckets; ++b)
  {
    offsets[b] = total;
    total += counts[b];
  }
  if (out_ranges)
  {
    out_ranges->clear();
    out_ranges->reserve(32);
    for (size_t b = 0; b < kBlockTypeBuckets; ++b)
    {
      if (counts[b] == 0)
      {
        continue;
      }
      const BlockId bid = static_cast<BlockId>(b);
      GpuBlockDrawRange range;
      range.blockId = bid;
      range.quadOffset = offsets[b];
      range.quadCount = counts[b];
      range.Transparent = registry.IsTransparent(bid);
      range.AlphaCutout =
          registry.GetRenderStyle(bid) == BlockRenderStyle::Cutout;
      out_ranges->push_back(range);
    }
  }
  scratch.resize(quads.size());
  for (const PackedQuad &q : quads)
  {
    const size_t b = static_cast<size_t>(q.BlockType());
    scratch[offsets[b]++] = q;
  }
  quads.swap(scratch);
  if (out_has_dark)
  {
    *out_has_dark = has_dark;
  }
}

void BuildRangesFromHistogram(const uint32_t *counts,
                              const uint32_t *exclusive_offsets,
                              UBlockRegistry &registry,
                              std::vector<GpuBlockDrawRange> *out_ranges)
{
  if (!out_ranges)
  {
    return;
  }
  out_ranges->clear();
  out_ranges->reserve(32);
  for (size_t b = 0; b < kBlockTypeBuckets; ++b)
  {
    if (counts[b] == 0)
    {
      continue;
    }
    const BlockId bid = static_cast<BlockId>(b);
    GpuBlockDrawRange range;
    range.blockId = bid;
    range.quadOffset = exclusive_offsets[b];
    range.quadCount = counts[b];
    range.Transparent = registry.IsTransparent(bid);
    range.AlphaCutout =
        registry.GetRenderStyle(bid) == BlockRenderStyle::Cutout;
    out_ranges->push_back(range);
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

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)

GLuint CompileSortCompute(const char *src, const char *label)
{
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[GpuMeshPipeline] " << label << " compile failed: " << log;
    glDeleteShader(sh);
    return 0;
  }
  const GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glDeleteShader(sh);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[GpuMeshPipeline] " << label << " link failed";
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

// Histogram + dark flag. counts[0..1023]=BlockType hist, counts[1024]=dark.
const char *kSortHistCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Quads { uvec2 quads[]; };
layout(std430, binding = 1) buffer Counts { uint counts[]; };
uniform uint numQuads;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= numQuads) return;
  uvec2 q = quads[i];
  uint bt = q.y & 0x3FFu;
  atomicAdd(counts[bt], 1u);
  uint face = (q.x >> 25u) & 0x7u;
  uint sky = (q.y >> 10u) & 0xFu;
  uint blk = (q.y >> 14u) & 0xFu;
  if (face != 5u && sky == 0u && blk == 0u) {
    atomicOr(counts[1024], 1u);
  }
}
)";

// Scatter by BlockType using exclusive offsets (atomically advanced).
const char *kSortScatterCompute = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Quads { uvec2 quads[]; };
layout(std430, binding = 1) buffer Offsets { uint offsets[]; };
layout(std430, binding = 2) writeonly buffer OutQuads { uvec2 outQuads[]; };
uniform uint numQuads;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= numQuads) return;
  uvec2 q = quads[i];
  uint bt = q.y & 0x3FFu;
  uint dst = atomicAdd(offsets[bt], 1u);
  outQuads[dst] = q;
}
)";

#endif

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

  SortHistProgram = CompileSortCompute(kSortHistCompute, "sort_hist");
  SortScatterProgram = CompileSortCompute(kSortScatterCompute, "sort_scatter");
  if (SortHistProgram && SortScatterProgram)
  {
    glGenBuffers(1, &SortCountsSsbo);
    glGenBuffers(1, &SortOffsetsSsbo);
    glGenBuffers(1, &SortScratchSsbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortCountsSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(kSortCountsWords * sizeof(uint32_t)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortOffsetsSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(kBlockTypeBuckets * sizeof(uint32_t)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortScratchSsbo);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(UGpuMeshSlotAllocator::kMaxQuadsPerSlot *
                                sizeof(PackedQuad)),
        nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    LOG(INFO) << "[GpuMeshPipeline] GPU BlockType counting-sort ready";
  }
  else
  {
    LOG(WARNING) << "[GpuMeshPipeline] GPU sort unavailable — CPU fallback";
    ShutdownGpuSort();
  }

  Ready = true;
  ScratchQuads.reserve(UGpuMeshSlotAllocator::kMaxQuadsPerSlot);
  ScratchQuadsSorted.reserve(UGpuMeshSlotAllocator::kMaxQuadsPerSlot);
  LOG(INFO) << "[GpuMeshPipeline] initialized with " << max_slots << " slots";
  return true;
#endif
}

void UGpuMeshPipeline::ShutdownGpuSort()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (SortHistProgram)
  {
    glDeleteProgram(SortHistProgram);
    SortHistProgram = 0;
  }
  if (SortScatterProgram)
  {
    glDeleteProgram(SortScatterProgram);
    SortScatterProgram = 0;
  }
  if (SortCountsSsbo)
  {
    glDeleteBuffers(1, &SortCountsSsbo);
    SortCountsSsbo = 0;
  }
  if (SortOffsetsSsbo)
  {
    glDeleteBuffers(1, &SortOffsetsSsbo);
    SortOffsetsSsbo = 0;
  }
  if (SortScratchSsbo)
  {
    glDeleteBuffers(1, &SortScratchSsbo);
    SortScratchSsbo = 0;
  }
#endif
}

void UGpuMeshPipeline::Shutdown()
{
  Allocator.Shutdown();
  while (!PendingQueue.empty())
  {
    PendingQueue.pop();
  }
  ShutdownGpuSort();
  Ready = false;
}

void UGpuMeshPipeline::EnqueueSnapshot(ChunkMeshSnapshot snapshot,
                                       uint64_t source_revision)
{
  PendingQueue.push({std::move(snapshot), source_revision});
}

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
bool UGpuMeshPipeline::GpuSortSlotQuads(
    uint32_t slot_offset, uint32_t num_quads, UBlockRegistry &registry,
    std::vector<GpuBlockDrawRange> *out_ranges, bool *out_has_dark_face)
{
  if (!SortHistProgram || !SortScatterProgram || num_quads == 0)
  {
    return false;
  }

  const GLsizeiptr quad_bytes =
      static_cast<GLsizeiptr>(num_quads * sizeof(PackedQuad));
  const GLintptr slot_byte_off =
      static_cast<GLintptr>(slot_offset * sizeof(PackedQuad));

  std::array<uint32_t, kSortCountsWords> zero_counts{};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortCountsSsbo);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero_counts),
                  zero_counts.data());

  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, Allocator.GetQuadSsbo(),
                    slot_byte_off, quad_bytes);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, SortCountsSsbo);
  glUseProgram(SortHistProgram);
  glUniform1ui(glGetUniformLocation(SortHistProgram, "numQuads"), num_quads);
  glDispatchCompute((num_quads + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // Histogram + dark only (~4KB) — not the full quad payload.
  std::array<uint32_t, kSortCountsWords> counts{};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortCountsSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counts), counts.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::array<uint32_t, kBlockTypeBuckets> offsets{};
  uint32_t total = 0;
  for (size_t b = 0; b < kBlockTypeBuckets; ++b)
  {
    offsets[b] = total;
    total += counts[b];
  }
  if (total != num_quads)
  {
    LOG(WARNING) << "[GpuMeshPipeline] sort hist total " << total
                 << " != numQuads " << num_quads;
    return false;
  }
  BuildRangesFromHistogram(counts.data(), offsets.data(), registry, out_ranges);
  if (out_has_dark_face)
  {
    *out_has_dark_face = counts[kBlockTypeBuckets] != 0;
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, SortOffsetsSsbo);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(offsets), offsets.data());

  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, Allocator.GetQuadSsbo(),
                    slot_byte_off, quad_bytes);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, SortOffsetsSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, SortScratchSsbo);
  glUseProgram(SortScatterProgram);
  glUniform1ui(glGetUniformLocation(SortScatterProgram, "numQuads"), num_quads);
  glDispatchCompute((num_quads + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

  glBindBuffer(GL_COPY_READ_BUFFER, SortScratchSsbo);
  glBindBuffer(GL_COPY_WRITE_BUFFER, Allocator.GetQuadSsbo());
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0,
                      slot_byte_off, quad_bytes);
  glBindBuffer(GL_COPY_READ_BUFFER, 0);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
  glUseProgram(0);
  return true;
}
#endif

bool UGpuMeshPipeline::RunComputePasses(const ChunkMeshSnapshot &snapshot,
                                        UBlockRegistry &registry,
                                        glm::ivec3 coord, int slot_idx,
                                        uint32_t &out_quad_count,
                                        std::vector<GpuBlockDrawRange> *out_ranges,
                                        bool *out_has_dark_face)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)snapshot;
  (void)registry;
  (void)coord;
  (void)slot_idx;
  (void)out_ranges;
  (void)out_has_dark_face;
  out_quad_count = 0;
  return false;
#else
  out_quad_count = 0;
  if (out_ranges)
  {
    out_ranges->clear();
  }
  if (out_has_dark_face)
  {
    *out_has_dark_face = false;
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

  // GPU hist+scatter uses SSBO atomics — on AMD iGPU always-on / mid thresholds
  // raised emerge_med (67→85). Keep the path compiled but disabled until a
  // non-atomic sort or discrete-GPU gate exists (max+1 ⇒ never).
  constexpr uint32_t kGpuSortMinQuads =
      UGpuMeshSlotAllocator::kMaxQuadsPerSlot + 1;
  const bool sorted_gpu =
      rect_count >= kGpuSortMinQuads &&
      GpuSortSlotQuads(slot_offset, rect_count, registry, out_ranges,
                       out_has_dark_face);
  if (!sorted_gpu)
  {
    ScratchQuads.resize(rect_count);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, Allocator.GetQuadSsbo());
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
        static_cast<GLsizeiptr>(ScratchQuads.size() * sizeof(PackedQuad)),
        ScratchQuads.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    CountingSortPackedQuadsByBlockType(ScratchQuads, ScratchQuadsSorted,
                                       registry, out_ranges, out_has_dark_face);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, Allocator.GetQuadSsbo());
    glBufferSubData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
        static_cast<GLsizeiptr>(ScratchQuads.size() * sizeof(PackedQuad)),
        ScratchQuads.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  Allocator.SetSlotQuadCount(slot_idx, rect_count);
  out_quad_count = rect_count;
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
  if (!RunComputePasses(snapshot, registry, coord, slot_idx, quad_count,
                        &out_result.blockRanges, &out_result.hasFullyDarkFace))
  {
    Allocator.FreeSlot(coord);
    return false;
  }

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
