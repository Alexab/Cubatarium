#pragma once

#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnFlowScheduler.h"

#include <cstdint>

namespace cutum
{

/// Result of exclusive ColumnEmergeState bump (Minecraft ChunkStatus-style).
enum class ColumnEmergeBumpResult : uint8_t
{
  Applied = 0,
  Noop = 1,
  Denied = 2,
};

inline int ColumnEmergeStateRank(ColumnEmergeState state)
{
  return static_cast<int>(state);
}

/// Repair may regress from LitReady+ back to Lighting or Meshing only.
inline bool IsColumnEmergeRepairRegression(ColumnEmergeState from,
                                           ColumnEmergeState to)
{
  if (from < ColumnEmergeState::LitReady)
  {
    return false;
  }
  return to == ColumnEmergeState::Lighting || to == ColumnEmergeState::Meshing;
}

/// Exclusive bump: forward (including skip) or allowed repair. Same state is
/// Noop. LitReady finalize on an already-Meshing/RenderReady column is Noop
/// (not a competing producer). Illegal regression (e.g. Meshing → Generating)
/// is Denied.
inline ColumnEmergeBumpResult TryAcquireColumnEmergeBump(
    ColumnEmergeState current, ColumnEmergeState requested)
{
  if (current == requested)
  {
    return ColumnEmergeBumpResult::Noop;
  }
  if (ColumnEmergeStateRank(requested) > ColumnEmergeStateRank(current))
  {
    return ColumnEmergeBumpResult::Applied;
  }
  if (IsColumnEmergeRepairRegression(current, requested))
  {
    return ColumnEmergeBumpResult::Applied;
  }
  // MarkRelit/commit often re-assert LitReady after mesh already started.
  if (requested == ColumnEmergeState::LitReady &&
      current > ColumnEmergeState::LitReady)
  {
    return ColumnEmergeBumpResult::Noop;
  }
  return ColumnEmergeBumpResult::Denied;
}

/// FirstMesh > RelightThenMesh > PromoteRelight > RemeshSeam (MC HP vs LP).
inline int ColumnWorkKindExclusiveRank(ColumnWorkKind kind)
{
  switch (kind)
  {
  case ColumnWorkKind::FirstMesh:
    return 3;
  case ColumnWorkKind::RelightThenMesh:
    return 2;
  case ColumnWorkKind::PromoteRelight:
    return 1;
  case ColumnWorkKind::RemeshSeam:
  default:
    return 0;
  }
}

} // namespace cutum
