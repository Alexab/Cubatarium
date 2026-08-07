#pragma once

#include <cstdint>

namespace cutum
{

/// Era14 DesiredStage: derive next column work from SoT without wall-gated
/// Imm as primary FirstMesh (TD-ARCH-041/042).
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

inline ColumnDesiredDecision DeriveColumnDesiredStage(bool missing_visible,
                                                      bool stale_focus,
                                                      bool void_focus,
                                                      bool pending_light)
{
  ColumnDesiredDecision out;
  if (missing_visible)
  {
    out.stage = ColumnDesiredStage::FirstMesh;
    return out;
  }
  if (stale_focus)
  {
    out.stage = ColumnDesiredStage::RemeshSeam;
    return out;
  }
  if (void_focus)
  {
    out.stage = ColumnDesiredStage::RelightOnly;
    return out;
  }
  if (pending_light)
  {
    out.stage = ColumnDesiredStage::RelightThenMesh;
  }
  return out;
}

} // namespace cutum
