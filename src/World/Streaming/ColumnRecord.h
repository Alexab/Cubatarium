#pragma once

#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnJobGraph.h"
#include "World/Streaming/ColumnVisualState.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

namespace cutum
{

/// Published draw residency (independent of pending replacement work).
struct ColumnPublishedState
{
  uint32_t mesh_version{0};
  /// Opaque residency token; 0 = none. Render maps to real GPU handle.
  uint64_t gpu_handle{0};
  uint32_t bounds_version{0};
  /// 1 when render_ready mirrored without a real GPU handle (shadow cutover).
  bool shadow_synthetic{false};
};

/// In-flight pipeline work (may coexist with published).
struct ColumnPendingState
{
  uint64_t token{0};
  ColumnJobStage stage{ColumnJobStage::Absent};
  uint32_t deps{0};
  ColumnJobPriority priority{ColumnJobPriority::Background};
};

/// Repair / backlog debt marker (scheduling hints, not visual truth).
struct ColumnDebtState
{
  uint8_t reason{0};
  uint64_t created_at_ms{0};
  uint64_t last_progress_at_ms{0};
};

/// Phase 2+ SoT: one runtime record per ground column (xz).
/// Dual-written with legacy ColumnEmergeStates until cutover.
struct ColumnRecord
{
  ColumnEmergeState emerge{ColumnEmergeState::Empty};
  ColumnDesiredStage desired{ColumnDesiredStage::None};
  uint32_t content_rev{0};
  uint32_t light_rev{0};
  uint32_t mesh_rev{0};
  /// 0 = idle; else opaque job token (async mesh / relight / gpu).
  uint64_t inflight_job{0};
  bool pending_light{false};
  bool sticky_remesh{false};
  bool light_complete_disk{false};
  bool raa_pending{false};
  /// Column has resident voxel data (ground chunk present).
  bool resident{false};
  ColumnPublishedState published{};
  ColumnPendingState pending{};
  ColumnDebtState debt{};
  /// Sysreset v2: owning visual FSM (mesh+light co-publish).
  ColumnVisualState visual{ColumnVisualState::Ready};
  /// Frames with real publish progress (lit rev advance / MarkRelit→Dirty).
  /// Not "GPU queued". PreferKick requires this > 0 while Publishing.
  int publish_progress_frames{0};
  /// PreferKick-without-Dirty stall counter while NeedRelight.
  int prefer_kick_stall_frames{0};
  /// Sysreset v3: sticky/overlay face debt (bit per face 0..5). X-ray until remesh.
  uint8_t face_debt_mask{0};
  /// Frames face debt has been outstanding (stall escape / telemetry).
  int face_debt_frames{0};
};

inline uint64_t PackColumnKey(glm::ivec2 xz)
{
  return (static_cast<uint64_t>(static_cast<uint32_t>(xz.x)) << 32) |
         static_cast<uint32_t>(xz.y);
}

inline bool ColumnHasPublishedRender(const ColumnRecord &rec)
{
  return rec.published.gpu_handle != 0 || rec.published.mesh_version > 0;
}

inline bool ColumnHasActivePending(const ColumnRecord &rec)
{
  return rec.pending.token != 0;
}

/// Thin store mirrored from SetColumnEmergeState / ticket updates.
class UColumnRecordStore
{
public:
  ColumnRecord &GetOrCreate(glm::ivec2 xz)
  {
    return Records[PackColumnKey(xz)];
  }

  const ColumnRecord *Find(glm::ivec2 xz) const
  {
    const auto it = Records.find(PackColumnKey(xz));
    return it == Records.end() ? nullptr : &it->second;
  }

  void SetEmerge(glm::ivec2 xz, ColumnEmergeState state)
  {
    GetOrCreate(xz).emerge = state;
  }

  void SetDesired(glm::ivec2 xz, ColumnDesiredStage stage)
  {
    GetOrCreate(xz).desired = stage;
  }

  void SetVisual(glm::ivec2 xz, ColumnVisualState state)
  {
    GetOrCreate(xz).visual = state;
  }

  void NotePublishProgress(glm::ivec2 xz)
  {
    ColumnRecord &rec = GetOrCreate(xz);
    if (rec.publish_progress_frames < 8)
    {
      ++rec.publish_progress_frames;
    }
    rec.prefer_kick_stall_frames = 0;
    if (rec.visual == ColumnVisualState::NeedRelight ||
        rec.visual == ColumnVisualState::NeedRemesh)
    {
      rec.visual = ColumnVisualState::Publishing;
    }
  }

  void NotePreferKickStall(glm::ivec2 xz)
  {
    ColumnRecord &rec = GetOrCreate(xz);
    ++rec.prefer_kick_stall_frames;
  }

  void NoteFaceDebt(glm::ivec2 xz, int face = -1)
  {
    ColumnRecord &rec = GetOrCreate(xz);
    if (face >= 0)
    {
      rec.face_debt_mask = NoteFaceDebtMask(rec.face_debt_mask, face);
    }
    else if (rec.face_debt_mask == 0)
    {
      // Unknown face: mark all horizontal bits as owed (seam coalesce).
      rec.face_debt_mask = 0x3Fu;
    }
    if (rec.face_debt_frames < 255)
    {
      ++rec.face_debt_frames;
    }
    if (rec.visual == ColumnVisualState::Ready)
    {
      rec.visual = ColumnVisualState::NeedRemesh;
    }
  }

  void ClearFaceDebt(glm::ivec2 xz, int face = -1)
  {
    ColumnRecord &rec = GetOrCreate(xz);
    if (face >= 0)
    {
      rec.face_debt_mask = ClearFaceDebtMask(rec.face_debt_mask, face);
    }
    else
    {
      rec.face_debt_mask = 0;
    }
    if (rec.face_debt_mask == 0)
    {
      rec.face_debt_frames = 0;
    }
  }

  void Erase(glm::ivec2 xz) { Records.erase(PackColumnKey(xz)); }

  void Clear() { Records.clear(); }

  size_t Size() const { return Records.size(); }

  template <typename Fn> void ForEach(Fn &&fn) const
  {
    for (const auto &kv : Records)
    {
      const int cx = static_cast<int>(static_cast<uint32_t>(kv.first >> 32));
      const int cz = static_cast<int>(static_cast<uint32_t>(kv.first));
      fn(glm::ivec2(cx, cz), kv.second);
    }
  }

private:
  std::unordered_map<uint64_t, ColumnRecord> Records;
};

} // namespace cutum
