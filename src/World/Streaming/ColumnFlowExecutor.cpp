#include "World/Streaming/ColumnFlowExecutor.h"

#include "World/Core/World.h"

namespace cutum
{

namespace
{
UColumnFlowExecutor gExecutor;
} // namespace

UColumnFlowExecutor &GetColumnFlowExecutor()
{
  return gExecutor;
}

void UColumnFlowExecutor::RequestPromoteRelight(glm::ivec2 near_column,
                                                int priority)
{
  Enqueue(near_column, ColumnWorkKind::PromoteRelight, priority);
}

void UColumnFlowExecutor::Dispatch(UWorld &world, const ColumnWorkItem &work,
                                   glm::ivec3 focus_ground_horiz,
                                   int focus_radius, int admit_batch)
{
  const glm::ivec2 focus_xz(focus_ground_horiz.x, focus_ground_horiz.z);
  const glm::ivec2 *only =
      (work.column.x == focus_xz.x && work.column.y == focus_xz.y) ? nullptr
                                                                  : &work.column;
  switch (work.kind)
  {
  case ColumnWorkKind::FirstMesh:
    world.AdmitFocusVisibleMissing(admit_batch, glm::vec2(0.0f), only);
    world.AdmitFocusMeshIngress(1);
    break;
  case ColumnWorkKind::RelightThenMesh:
    world.RecoverUnlitFocusMeshes(1, only);
    break;
  case ColumnWorkKind::RemeshSeam:
    world.SyncIdleFocusGreedyRemesh(1);
    break;
  case ColumnWorkKind::PromoteRelight:
    world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
    break;
  }
}

int UColumnFlowExecutor::DrainBudget(UWorld &world, int n,
                                     glm::ivec3 focus_ground_horiz,
                                     int focus_radius, int admit_batch)
{
  int drained = 0;
  ColumnWorkItem work{};
  while (drained < n && scheduler_.DrainOne(work))
  {
    Dispatch(world, work, focus_ground_horiz, focus_radius, admit_batch);
    ++drained;
  }
  return drained;
}

void UColumnFlowExecutor::TickDerived(UWorld &world,
                                      glm::ivec3 focus_ground_horiz,
                                      int /*focus_radius*/, bool moving,
                                      bool missing_visible_mesh,
                                      bool visual_holes, bool idle_remesh_debt,
                                      bool idle_focus_dirty_debt,
                                      int pending_focus_n, int recover_n,
                                      int admit_n)
{
  (void)world;
  const glm::ivec2 focus(focus_ground_horiz.x, focus_ground_horiz.z);
  if (recover_n > 0 && !idle_remesh_debt && !idle_focus_dirty_debt)
  {
    Enqueue(focus, ColumnWorkKind::RelightThenMesh, recover_n);
  }
  if (admit_n > 0 &&
      ((!moving && (missing_visible_mesh || pending_focus_n > 0) &&
        !idle_remesh_debt) ||
       (moving && visual_holes)))
  {
    Enqueue(focus, ColumnWorkKind::FirstMesh, admit_n + 50);
  }
}

void UColumnFlowExecutor::DrainIdlePendingLight(
    UWorld &world, glm::ivec3 focus_ground_horiz, int focus_radius, int budget,
    bool allow_sync, double /*last_frame_ms*/, int /*pending_focus_count*/,
    bool /*missing_visible_mesh*/)
{
  world.DrainIdleFocusPendingLight(focus_ground_horiz, focus_radius, budget);
  if (allow_sync)
  {
    world.DrainIdleFocusPendingLightSync(focus_ground_horiz, focus_radius, 1);
  }
}

} // namespace cutum
