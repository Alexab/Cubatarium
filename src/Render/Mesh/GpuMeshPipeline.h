#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Mesh/GpuMeshSlotAllocator.h"
#include "Render/Mesh/GpuPackedMeshTypes.h"
#include "Render/Mesh/PackedQuad.h"
#include "Blocks/BlockRegistry.h"
#include <glm/glm.hpp>
#include <memory>
#include <queue>
#include <vector>

namespace cutum
{

/// Full GPU mesh pipeline: snapshot upload → mask → greedy → packed emit → SSBO slot.
/// Replaces the CPU vertex readback path in GPF1 for eligible chunks.
/// Non-eligible chunks and cross instances continue through the CPU path.
class UGpuMeshPipeline
{
public:
  static constexpr uint32_t kDefaultMaxSlots = 2048;

  bool Init(uint32_t max_slots = kDefaultMaxSlots);
  void Shutdown();
  bool IsReady() const { return Ready; }

  /// Queue a snapshot for GPU meshing on the next frame.
  void EnqueueSnapshot(ChunkMeshSnapshot snapshot, uint64_t source_revision);

  /// Process one snapshot synchronously (main thread, GL context required).
  bool ProcessSnapshot(const ChunkMeshSnapshot &snapshot,
                       UBlockRegistry &registry,
                       GpuMeshProcessResult &out_result);

  /// Process queued snapshots: upload → compute → write to SSBO slots.
  int ProcessQueue(UBlockRegistry &registry, int budget);

  void FreeChunk(glm::ivec3 chunk_coord);

  bool HasGpuMesh(glm::ivec3 chunk_coord) const;

  UGpuMeshSlotAllocator &GetAllocator() { return Allocator; }
  const UGpuMeshSlotAllocator &GetAllocator() const { return Allocator; }

  int GetPendingCount() const { return static_cast<int>(PendingQueue.size()); }

private:
  struct PendingChunk
  {
    ChunkMeshSnapshot Snapshot;
    uint64_t SourceRevision;
  };

  bool RunComputePasses(const ChunkMeshSnapshot &snapshot,
                        UBlockRegistry &registry, glm::ivec3 coord,
                        int slot_idx, uint32_t &out_quad_count);

  bool Ready{false};
  GpuGreedyEmitState EmitState;
  UGpuMeshSlotAllocator Allocator;
  std::queue<PendingChunk> PendingQueue;
};

} // namespace cutum
