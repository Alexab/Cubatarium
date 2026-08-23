#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Streaming/RelightInstallPlanner.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

enum class FakeMeshCallKind : uint8_t
{
  MarkDirty,
  MarkDirtyPriority,
  RequestRemeshAfterApply,
  PreferKickGpu,
};

struct FakeMeshCallRecord
{
  FakeMeshCallKind kind;
  glm::ivec3 coord;
};

struct FakeMeshChunkState
{
  bool has_greedy{false};
  bool has_drawable{false};
  bool is_dirty{false};
  bool raa_pending{false};
  bool gpu_pending{false};
  bool inflight{false};
  bool fully_dark{false};
  bool soft_defer{false};
  uint64_t meshed_light_rev{0};
  uint64_t light_field_rev{0};
};

/// Test double for MarkRelit planner/executor tests (FZ2.7-B0/B4).
class FakeMeshServiceForLitApply
{
public:
  std::unordered_map<glm::ivec3, FakeMeshChunkState, IVec3Hash> chunks;
  std::vector<FakeMeshCallRecord> calls;

  FakeMeshChunkState &Mut(glm::ivec3 coord)
  {
    return chunks[coord];
  }

  const FakeMeshChunkState &Get(glm::ivec3 coord) const
  {
    static const FakeMeshChunkState kEmpty{};
    const auto it = chunks.find(coord);
    return it != chunks.end() ? it->second : kEmpty;
  }

  bool HasDrawableGreedyMesh(glm::ivec3 coord) const
  {
    return Get(coord).has_drawable;
  }

  bool HasGreedyMesh(glm::ivec3 coord) const { return Get(coord).has_greedy; }

  bool IsChunkMeshDirty(glm::ivec3 coord) const { return Get(coord).is_dirty; }

  bool IsRemeshAfterApplyPending(glm::ivec3 coord) const
  {
    return Get(coord).raa_pending;
  }

  bool IsPendingGpuApply(glm::ivec3 coord) const
  {
    return Get(coord).gpu_pending;
  }

  bool HasInflightMeshBuild(glm::ivec3 coord) const
  {
    return Get(coord).inflight;
  }

  bool IsSoftDeferHeld(glm::ivec3 coord) const { return Get(coord).soft_defer; }

  bool ChunkHasFullyDarkFace(glm::ivec3 coord) const
  {
    return Get(coord).fully_dark;
  }

  bool ChunkIsLightStale(glm::ivec3 coord) const
  {
    const FakeMeshChunkState &st = Get(coord);
    return IsMeshLightStale(st.meshed_light_rev, st.light_field_rev);
  }

  void MarkDirty(glm::ivec3 coord)
  {
    Mut(coord).is_dirty = true;
    calls.push_back({FakeMeshCallKind::MarkDirty, coord});
  }

  void MarkDirtyPriority(glm::ivec3 coord)
  {
    Mut(coord).is_dirty = true;
    calls.push_back({FakeMeshCallKind::MarkDirtyPriority, coord});
  }

  void RequestRemeshAfterApply(glm::ivec3 coord)
  {
    Mut(coord).raa_pending = true;
    calls.push_back({FakeMeshCallKind::RequestRemeshAfterApply, coord});
  }

  void PreferKickPendingGpuQueued(glm::ivec3 coord)
  {
    calls.push_back({FakeMeshCallKind::PreferKickGpu, coord});
  }

  void ClearCalls() { calls.clear(); }

  int CountCalls(FakeMeshCallKind kind) const
  {
    int n = 0;
    for (const FakeMeshCallRecord &rec : calls)
    {
      if (rec.kind == kind)
      {
        ++n;
      }
    }
    return n;
  }
};

inline ColumnChunkSnapshot SnapshotFromFake(const FakeMeshServiceForLitApply &mesh,
                                            glm::ivec3 coord)
{
  ColumnChunkSnapshot snap;
  snap.coord = coord;
  const FakeMeshChunkState &st = mesh.Get(coord);
  snap.has_greedy = st.has_greedy;
  snap.has_drawable = st.has_drawable;
  snap.is_dirty = st.is_dirty;
  snap.raa_pending = st.raa_pending;
  snap.gpu_pending = st.gpu_pending;
  snap.inflight = st.inflight;
  snap.fully_dark = st.fully_dark;
  snap.soft_defer = st.soft_defer;
  snap.meshed_light_rev = st.meshed_light_rev;
  snap.light_field_rev = st.light_field_rev;
  snap.still_stale = mesh.ChunkIsLightStale(coord);
  return snap;
}

struct FakeLitApplyFsmOutcome
{
  bool erased_pending{false};
  bool erased_inflight{false};
  ColumnEmergeState fsm{ColumnEmergeState::LitReady};
  bool persistence_light_complete{false};
};

inline FakeLitApplyFsmOutcome ExecuteLitApplyPlanOnFake(
    FakeMeshServiceForLitApply &mesh, const LitApplyPlan &plan)
{
  FakeLitApplyFsmOutcome out;
  for (const glm::ivec3 &coord : plan.prefer_kick_gpu)
  {
    mesh.PreferKickPendingGpuQueued(coord);
  }
  for (const glm::ivec3 &coord : plan.request_raa)
  {
    mesh.RequestRemeshAfterApply(coord);
  }
  for (const glm::ivec3 &coord : plan.mark_dirty_priority)
  {
    mesh.MarkDirtyPriority(coord);
  }
  for (const glm::ivec3 &coord : plan.mark_dirty)
  {
    mesh.MarkDirty(coord);
  }
  out.erased_pending = plan.erase_pending_light;
  out.erased_inflight = plan.erase_inflight;
  out.fsm = plan.fsm_after;
  out.persistence_light_complete = plan.persistence_light_complete;
  return out;
}

} // namespace cutum
