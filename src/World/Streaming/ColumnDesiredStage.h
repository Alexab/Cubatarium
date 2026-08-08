#pragma once

#include <cstdint>

namespace cutum
{

/// Era14 DesiredStage: derive next column work from SoT without wall-gated
/// Imm as primary FirstMesh (TD-ARCH-041/042).
/// Era15 TD-050: lit_pending / unlit_published feed RemeshSeam / RelightThenMesh.
enum class ColumnDesiredStage : uint8_t
{
  None = 0,
  FirstMesh,
  RelightThenMesh,
  RemeshSeam,
  RelightOnly,
};

struct ColumnDesiredDecision
{
  ColumnDesiredStage stage{ColumnDesiredStage::None};
  bool enqueue_without_wall_gate{true};
};

inline ColumnDesiredDecision DeriveColumnDesiredStage(
    bool missing_visible, bool stale_focus, bool void_focus,
    bool pending_light, bool lit_pending = false,
    bool unlit_published = false)
{
  ColumnDesiredDecision out;
  if (missing_visible)
  {
    out.stage = ColumnDesiredStage::FirstMesh;
    return out;
  }
  // Era19 P2 I-B3/exclusivity: PendingLight owns the column — no RemeshSeam
  // dual with Relight in the same TickDerived decision.
  if (pending_light)
  {
    out.stage = ColumnDesiredStage::RelightThenMesh;
    return out;
  }
  // LitPending / stale drawable → remesh (Unlit→Lit guarantee) after light.
  if (lit_pending || stale_focus)
  {
    out.stage = ColumnDesiredStage::RemeshSeam;
    return out;
  }
  // Void + UnlitPublished needs RelightThenMesh (not RelightOnly) so mesh
  // follows light; bare RelightOnly left black Unlit forever (manual 093701).
  if (void_focus)
  {
    out.stage = unlit_published ? ColumnDesiredStage::RelightThenMesh
                                : ColumnDesiredStage::RelightOnly;
    return out;
  }
  return out;
}

} // namespace cutum
