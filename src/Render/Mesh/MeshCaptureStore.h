#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Streaming/WorkToken.h"
#include "Core/Jobs/PipelineAdmission.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

/// Immutable mesh capture cache (TD-ARCH-015 / main-thread offload Phase 1).
class UMeshCaptureStore
{
public:
  void Invalidate(glm::ivec3 coord);
  void InvalidateAll();
  void BumpWorldEpoch();
  uint64_t WorldEpoch() const { return WorldEpoch_; }

  std::optional<ChunkMeshSnapshot> TryGet(const UBlockWorld &world, glm::ivec3 coord,
                                          uint64_t source_revision) const;

  /// Store capture result. Rejects when `world_epoch` != current epoch (M08).
  /// Optional credit lives until entry eviction (audit R12).
  bool TryCommit(glm::ivec3 coord, uint64_t source_revision,
                 uint64_t world_epoch, ChunkMeshSnapshot snapshot,
                 const DependencyStamp *expected_deps = nullptr,
                 std::unique_ptr<UPipelineCreditGuard> credit = nullptr);

  void Commit(glm::ivec3 coord, uint64_t source_revision,
              uint64_t world_epoch, ChunkMeshSnapshot snapshot,
              std::unique_ptr<UPipelineCreditGuard> credit = nullptr);

  /// Capture + commit. Returns nullopt when snapshot admission credit is
  /// exhausted (Q7/R2) — never a fake empty Ready snapshot.
  std::optional<ChunkMeshSnapshot> CaptureAndStore(const UBlockWorld &world,
                                                   glm::ivec3 coord,
                                                   uint64_t source_revision);

  std::optional<ChunkMeshSnapshot> TakeOrRefresh(
      const UBlockWorld &world, glm::ivec3 coord, uint64_t source_revision,
      int &refresh_budget);

  std::optional<ChunkMeshSnapshot> RefreshIncrementalShell(
      const UBlockWorld &world, glm::ivec3 coord, uint64_t source_revision,
      uint8_t face_mask);

  int LastStoreHitN() const { return LastStoreHitN_; }
  int LastStoreMissN() const { return LastStoreMissN_; }
  void ResetStoreHitCounters()
  {
    LastStoreHitN_ = 0;
    LastStoreMissN_ = 0;
  }

  void SetNeighborVisualDrawableFn(
      ChunkMeshSnapshot::NeighborVisualDrawableFn fn, void *ctx)
  {
    NeighborDrawableFn_ = fn;
    NeighborDrawableCtx_ = ctx;
  }

  size_t Size() const { return Store_.size(); }

  ChunkMeshSnapshot::NeighborVisualDrawableFn GetNeighborDrawableFn() const
  {
    return NeighborDrawableFn_;
  }
  void *GetNeighborDrawableCtx() const { return NeighborDrawableCtx_; }

private:
  struct Entry
  {
    uint64_t worldEpoch{0};
    uint64_t sourceRevision{0};
    DependencyStamp deps{};
    ChunkMeshSnapshot data;
    std::unique_ptr<UPipelineCreditGuard> credit;
  };
  uint64_t WorldEpoch_{1};
  std::unordered_map<glm::ivec3, Entry, IVec3Hash> Store_;
  ChunkMeshSnapshot::NeighborVisualDrawableFn NeighborDrawableFn_{nullptr};
  void *NeighborDrawableCtx_{nullptr};
  int LastStoreHitN_{0};
  int LastStoreMissN_{0};
};

} // namespace cutum
