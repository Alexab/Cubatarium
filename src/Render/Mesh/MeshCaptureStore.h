#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/ChunkManager.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

/// Immutable mesh capture cache (TD-ARCH-015 / main-thread offload Phase 1).
/// Schedule prefers stored snapshots; live Capture only on miss/epoch mismatch
/// within a per-tick budget.
///
/// Era14 TD-ARCH-046: worker-side Capture remains deferred. Prior worker path
/// hung; store-only consumer is required before re-enable. Throughput knobs =
/// refresh budget + MeshSnapshotBudgetMs (not Imm/zoo).
class UMeshCaptureStore
{
public:
  void Invalidate(glm::ivec3 coord);
  void InvalidateAll();
  void BumpWorldEpoch();
  uint64_t WorldEpoch() const { return WorldEpoch_; }

  /// Return stored snapshot if epoch matches; otherwise nullopt.
  std::optional<ChunkMeshSnapshot> TryGet(glm::ivec3 coord,
                                          uint64_t source_revision) const;

  /// Capture from live world and store (main thread).
  ChunkMeshSnapshot CaptureAndStore(const UBlockWorld &world, glm::ivec3 coord,
                                    uint64_t source_revision);

  /// Prefer store; refresh via live Capture at most `refresh_budget` times.
  ChunkMeshSnapshot TakeOrRefresh(const UBlockWorld &world, glm::ivec3 coord,
                                  uint64_t source_revision,
                                  int &refresh_budget);

  /// Era39: SoftDefer-hidden neighbors → Air shell (capture-time).
  void SetNeighborVisualDrawableFn(
      ChunkMeshSnapshot::NeighborVisualDrawableFn fn, void *ctx)
  {
    NeighborDrawableFn_ = fn;
    NeighborDrawableCtx_ = ctx;
  }

  size_t Size() const { return Store_.size(); }

private:
  struct Entry
  {
    uint64_t worldEpoch{0};
    uint64_t sourceRevision{0};
    ChunkMeshSnapshot data;
  };
  uint64_t WorldEpoch_{1};
  std::unordered_map<glm::ivec3, Entry, IVec3Hash> Store_;
  ChunkMeshSnapshot::NeighborVisualDrawableFn NeighborDrawableFn_{nullptr};
  void *NeighborDrawableCtx_{nullptr};
};

} // namespace cutum
