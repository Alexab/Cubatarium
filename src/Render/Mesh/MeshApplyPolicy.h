#pragma once

#include <cstdint>

namespace cutum
{

/// Decision for async/GPU mesh apply vs ActiveMeshSourceRevision + Current.
/// Fixes thrash where an older result erased Active tracking for a newer build
/// (manual_1957: mesh_apply_stale≈392, dirty plateau).
enum class MeshApplyRevDecision : uint8_t
{
  Commit = 0,
  /// Active tracks a different (newer) rev — drop this result, keep Active.
  DiscardOlderKeepActive,
  /// Active matches result but Current moved — remesh without bump.
  RemeshObsoleteTracked,
  /// No Active entry — drop; do not Dirty (CancelOutside / cancelled).
  DropNoActive,
};

inline MeshApplyRevDecision ClassifyMeshApplyRevision(bool has_active,
                                                      uint64_t active_rev,
                                                      uint64_t result_rev,
                                                      uint64_t current_rev)
{
  if (!has_active)
  {
    return MeshApplyRevDecision::DropNoActive;
  }
  if (active_rev != result_rev)
  {
    return MeshApplyRevDecision::DiscardOlderKeepActive;
  }
  if (result_rev != current_rev)
  {
    return MeshApplyRevDecision::RemeshObsoleteTracked;
  }
  return MeshApplyRevDecision::Commit;
}

/// Era15 TD-ARCH-049 MeshResidency: when replacing a live GPU-resident mesh with
/// CPU batches, publish the CPU drawable *before* FreeChunk. Free-first opens a
/// one-frame hole whenever GPU was the sole drawable source (batches cleared on
/// GPU commit). Industry: keep old mesh until replacement is ready.
inline bool ShouldPublishCpuBatchesBeforeFreeGpu()
{
  return true;
}

/// True when FreeChunk-before-write would drop HasDrawable (GPU-only drawable).
inline bool CpuReplaceFreeFirstWouldHole(bool gpu_drawable,
                                         bool new_cpu_batches_drawable)
{
  return gpu_drawable && !new_cpu_batches_drawable;
}

} // namespace cutum
