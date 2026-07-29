#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Mesh/GpuMeshSlotAllocator.h"
#include "Render/Mesh/PackedQuad.h"
#include "Blocks/BlockRegistry.h"
#include <glm/glm.hpp>
#include <memory>
#include <queue>
#include <vector>

namespace cutum
{

/// Full GPU mesh pipeline: snapshot upload → mask → greedy → packed emit → SSBO slot.
/// Replaces the CPU readback path in GPF1 for eligible chunks.
/// Non-eligible chunks and cross instances continue through the CPU path.
class UGpuMeshPipeline
{
public:
  static constexpr uint32_t kDefaultMaxSlots = 2048;

  bool Init(uint32_t max_slots = kDefaultMaxSlots);
  void Shutdown();
  bool IsReady() const { return Ready; }

  /// Queue a snapshot for GPU meshing on the next frame.
  void EnqueueSnapshot(ChunkMeshSnapshot snapshot,
                       uint64_t source_revision);

  /// Process queued snapshots: upload → compute → write to SSBO slots.
  /// Call once per frame on the main thread with GL context.
  /// Returns number of chunks processed.
  int ProcessQueue(UBlockRegistry &registry, int budget);

  /// Free the GPU slot for a chunk (when chunk is unloaded or remeshed by CPU).
  void FreeChunk(glm::ivec3 chunk_coord);

  /// Build draw commands for the current visible set and issue MDI draws.
  void BuildAndDrawOpaque();
  void BuildAndDrawTransparent();

  /// Check if a chunk has GPU-resident mesh data.
  bool HasGpuMesh(glm::ivec3 chunk_coord) const;

  /// Get the slot allocator for direct access.
  UGpuMeshSlotAllocator &GetAllocator() { return Allocator; }
  const UGpuMeshSlotAllocator &GetAllocator() const { return Allocator; }

  int GetPendingCount() const { return static_cast<int>(PendingQueue.size()); }

private:
  struct PendingChunk
  {
    ChunkMeshSnapshot Snapshot;
    uint64_t SourceRevision;
  };

  bool Ready{false};
  GpuGreedyEmitState EmitState;
  UGpuMeshSlotAllocator Allocator;
  std::queue<PendingChunk> PendingQueue;
};

} // namespace cutum
