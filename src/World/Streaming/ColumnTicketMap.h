#pragma once

#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/VisualStagePolicy.h"
#include <algorithm>
#include <cstdint>

namespace cutum
{

/// Phase 3: MC-style ticket level from Chebyshev ring (higher = more work owed).
enum class ColumnTicketLevel : uint8_t
{
  None = 0,
  GenKeep = 1,
  Light = 2,
  MeshDeferred = 3, // ring3+ LOD / deferred greedy
  MeshLit = 4,      // ring ≤ LitDrawable
  MeshFull = 5,     // ring ≤ NearFov
};

inline ColumnTicketLevel TicketLevelForRing(int chebyshev_horiz)
{
  const int h = std::max(0, chebyshev_horiz);
  if (h <= kVisualStageNearFovHoriz)
  {
    return ColumnTicketLevel::MeshFull;
  }
  if (h <= kVisualStageLitDrawableHoriz)
  {
    return ColumnTicketLevel::MeshLit;
  }
  if (h <= kVisualStageLitDrawableHoriz + 2)
  {
    return ColumnTicketLevel::MeshDeferred;
  }
  if (h <= kVisualStageLitDrawableHoriz + 4)
  {
    return ColumnTicketLevel::Light;
  }
  return ColumnTicketLevel::GenKeep;
}

inline ColumnDesiredStage DesiredStageFromTicket(
    ColumnTicketLevel ticket, ColumnEmergeState achieved, bool missing_mesh,
    bool pending_light, bool void_or_dark)
{
  if (ticket == ColumnTicketLevel::None)
  {
    return ColumnDesiredStage::None;
  }
  if (missing_mesh &&
      static_cast<uint8_t>(ticket) >=
          static_cast<uint8_t>(ColumnTicketLevel::MeshDeferred))
  {
    return ColumnDesiredStage::FirstMesh;
  }
  if (pending_light &&
      static_cast<uint8_t>(ticket) >=
          static_cast<uint8_t>(ColumnTicketLevel::Light))
  {
    return ColumnDesiredStage::RelightThenMesh;
  }
  if (void_or_dark &&
      static_cast<uint8_t>(ticket) >=
          static_cast<uint8_t>(ColumnTicketLevel::MeshLit))
  {
    return ColumnDesiredStage::RelightThenMesh;
  }
  if (static_cast<uint8_t>(achieved) <
          static_cast<uint8_t>(ColumnEmergeState::Meshing) &&
      static_cast<uint8_t>(ticket) >=
          static_cast<uint8_t>(ColumnTicketLevel::MeshLit))
  {
    return ColumnDesiredStage::FirstMesh;
  }
  if (static_cast<uint8_t>(achieved) >=
          static_cast<uint8_t>(ColumnEmergeState::LitReady) &&
      ticket == ColumnTicketLevel::MeshFull)
  {
    return ColumnDesiredStage::RemeshSeam;
  }
  return ColumnDesiredStage::None;
}

/// Phase 3 fixed pools — sizes only; HoleDrain redistributes, no new floors.
struct WorkPoolBudget
{
  int gen_slots{2};
  int light_slots{2};
  int first_mesh_slots{6};
  int remesh_slots{2};
  int gpu_upload_slots{4};
};

inline WorkPoolBudget DefaultCruisePools()
{
  return WorkPoolBudget{};
}

inline WorkPoolBudget HoleDrainPools(const WorkPoolBudget &base)
{
  WorkPoolBudget out = base;
  // Closeout C: keep 1 remesh reservation; steal the rest into FirstMesh.
  const int steal = std::max(0, out.remesh_slots - 1);
  out.first_mesh_slots += steal;
  out.remesh_slots = std::min(1, out.remesh_slots);
  out.light_slots = std::max(out.light_slots, 3);
  return out;
}

inline WorkPoolBudget ApplyPoolsToAdmissionCaps(WorkPoolBudget pools,
                                                int &out_first_mesh,
                                                int &out_remesh,
                                                int &out_gpu)
{
  out_first_mesh = pools.first_mesh_slots;
  out_remesh = pools.remesh_slots;
  out_gpu = pools.gpu_upload_slots;
  return pools;
}

} // namespace cutum
