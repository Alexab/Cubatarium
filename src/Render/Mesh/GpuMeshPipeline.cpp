#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"

namespace cutum
{

bool UGpuMeshPipeline::Init(uint32_t max_slots)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)max_slots;
  return false;
#else
  if (!EnsureGpuOpaqueEmit(EmitState))
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
    PendingChunk &pending = PendingQueue.front();
    const glm::ivec3 coord = pending.Snapshot.coord;

    if (!SnapshotIsGpuExtractEligible(pending.Snapshot, registry))
    {
      PendingQueue.pop();
      continue;
    }

    const bool has_transparent = [&]()
    {
      for (BlockId id : pending.Snapshot.blocks)
      {
        if (id != 0 && (registry.IsTransparent(id) ||
                        registry.GetRenderStyle(id) == BlockRenderStyle::Cutout))
        {
          return true;
        }
      }
      return false;
    }();

    const int slot_idx = Allocator.AllocateSlot(coord, has_transparent);
    if (slot_idx < 0)
    {
      LOG(WARNING) << "[GpuMeshPipeline] no free slots";
      PendingQueue.pop();
      continue;
    }

    // Run mask → greedy → packed emit compute pipeline.
    // The packed emit writes directly into the allocator's SSBO at the slot offset.
    std::vector<uint8_t> occ;
    BuildPaddedOccupancy(pending.Snapshot, registry, occ);
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
          static_cast<uint8_t>(pending.Snapshot.blocks[static_cast<size_t>(i)]);
    }
    std::vector<uint8_t> padded_lights;
    BuildPaddedLight(pending.Snapshot, padded_lights);
    std::vector<uint32_t> block_words, light_words;
    block_words.assign((blocks.size() + 3) / 4, 0);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
      block_words[i >> 2] |=
          static_cast<uint32_t>(blocks[i]) << ((i & 3u) * 8u);
    }
    light_words.assign((padded_lights.size() + 3) / 4, 0);
    for (size_t i = 0; i < padded_lights.size(); ++i)
    {
      light_words[i >> 2] |=
          static_cast<uint32_t>(padded_lights[i]) << ((i & 3u) * 8u);
    }

    const uint32_t volume = static_cast<uint32_t>(CHUNK_VOLUME);
    const uint32_t side = static_cast<uint32_t>(CHUNK_SIZE);

    // Upload occupancy, blocks, lights
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

    // Pass 1: Mask
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, EmitState.OccSsbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, EmitState.MaskSsbo);
    glUseProgram(EmitState.MaskProgram);
    glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "volume"), volume);
    glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "side"), side);
    glUniform1ui(glGetUniformLocation(EmitState.MaskProgram, "pad"),
                 static_cast<uint32_t>(kGpuOccPad));
    glDispatchCompute((volume + 63u) / 64u, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Pass 2: Greedy
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

    // Read rect count
    std::array<uint32_t, 4> counters{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, EmitState.CountersSsbo);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters),
                       counters.data());
    const uint32_t rect_count = counters[0];

    if (rect_count > 0 && rect_count <= UGpuMeshSlotAllocator::kMaxQuadsPerSlot &&
        EmitState.PackedEmitProgram != 0)
    {
      // Pass 3: Packed emit → directly into allocator's SSBO at slot offset
      const uint32_t slot_offset = Allocator.GetSlot(coord)->OffsetQuads;
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, EmitState.RectsSsbo);
      glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1,
                        Allocator.GetQuadSsbo(),
                        static_cast<GLintptr>(slot_offset * sizeof(PackedQuad)),
                        static_cast<GLsizeiptr>(rect_count * sizeof(PackedQuad)));
      glUseProgram(EmitState.PackedEmitProgram);
      glUniform1ui(
          glGetUniformLocation(EmitState.PackedEmitProgram, "numRects"),
          rect_count);
      glDispatchCompute((rect_count + 63u) / 64u, 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
      Allocator.SetSlotQuadCount(slot_idx, rect_count);
    }
    else
    {
      Allocator.SetSlotQuadCount(slot_idx, 0);
    }

    glUseProgram(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    PendingQueue.pop();
    ++processed;
  }

  return processed;
#endif
}

void UGpuMeshPipeline::FreeChunk(glm::ivec3 chunk_coord)
{
  Allocator.FreeSlot(chunk_coord);
}

void UGpuMeshPipeline::BuildAndDrawOpaque()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!Ready)
  {
    return;
  }
  Allocator.BuildOpaqueDrawCommands();
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, Allocator.GetQuadSsbo());
  Allocator.DrawOpaque();
#endif
}

void UGpuMeshPipeline::BuildAndDrawTransparent()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!Ready)
  {
    return;
  }
  Allocator.BuildTransparentDrawCommands();
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, Allocator.GetQuadSsbo());
  Allocator.DrawTransparent();
#endif
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
