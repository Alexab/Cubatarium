#pragma once

#include <cstdint>

namespace cutum
{

/// Era14 DesiredStage: derive next column work from SoT without wall-gated
/// Imm as primary FirstMesh (TD-ARCH-041/042).
/// Era15 TD-050: lit_pending / unlit_published feed RemeshSeam / RelightThenMesh.
/// Era32 P1: lit_pending = post-light remesh only (not VB count → RemeshSeam).
/// void + dark drawable → RelightThenMesh (not RelightOnly).
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
    bool unlit_published = false, bool dark_drawable = false)
{
  ColumnDesiredDecision out;
  // Era19 exclusivity + Era28 Relight-before-draw: PendingLight owns the
  // column even when mesh is missing. FirstMesh under pending is Unlit near.
  if (pending_light)
  {
    out.stage = ColumnDesiredStage::RelightThenMesh;
    return out;
  }
  if (missing_visible)
  {
    out.stage = ColumnDesiredStage::FirstMesh;
    return out;
  }
  // Void / dark drawable needs RelightThenMesh so mesh follows light.
  if (void_focus || dark_drawable)
  {
    out.stage = ColumnDesiredStage::RelightThenMesh;
    return out;
  }
  // LitPending after light proof / sticky remesh-on-lit → RemeshSeam.
  if (lit_pending || stale_focus)
  {
    out.stage = ColumnDesiredStage::RemeshSeam;
    return out;
  }
  (void)unlit_published;
  return out;
}

} // namespace cutum
