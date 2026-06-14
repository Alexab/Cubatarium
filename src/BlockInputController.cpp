#include "BlockInputController.h"

#include "Application.h"
#include "Camera.h"
#include "Creature.h"
#include "CreatureInventory.h"
#include "GeometryEngine.h"
#include "InventoryTypes.h"
#include "World.h"

#include <GLFW/glfw3.h>
#include <cmath>

namespace cutum
{

namespace
{

float CursorDragDistancePx(glm::vec2 a, glm::vec2 b)
{
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

} // namespace

const InventoryEntryRef *
UBlockInputController::GetActiveEntry(const BlockInputContext &ctx) const
{
  if (!ctx.World)
  {
    return nullptr;
  }
  if (UCreature *controlled = ctx.World->GetControlledCreature())
  {
    return controlled->GetInventory().GetActiveEntryRef();
  }
  return nullptr;
}

bool UBlockInputController::ActiveSlotBlocksWorldInteraction(
    const BlockInputContext &ctx) const
{
  const InventoryEntryRef *active = GetActiveEntry(ctx);
  if (!active)
  {
    return false;
  }
  if (active->kind == InventoryEntryKind::UCreature && !active->id.empty())
  {
    return true;
  }
  if (active->kind == InventoryEntryKind::Skin && !active->id.empty())
  {
    return true;
  }
  return false;
}

void UBlockInputController::TryPlaceFromActiveSlot(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return;
  }
  const InventoryEntryRef *active = GetActiveEntry(ctx);
  if (active && active->kind == InventoryEntryKind::UObject &&
      !active->id.empty())
  {
    ctx.World->PlaceActivePrefabByView();
  }
  else
  {
    ctx.World->AddObjectByView();
  }
}

void UBlockInputController::TrySpawnCreatureOrSkin(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return;
  }
  const InventoryEntryRef *active = GetActiveEntry(ctx);
  if (!active)
  {
    return;
  }
  if (active->kind == InventoryEntryKind::UCreature && !active->id.empty())
  {
    if (!ctx.World->SpawnCreatureByView(active->id) && ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage("Cannot spawn " + active->id, 2.0);
    }
    return;
  }
  if (active->kind == InventoryEntryKind::Skin && !active->id.empty())
  {
    auto camera = ctx.World->GetCurrentUserCamera();
    if (!camera)
    {
      return;
    }
    const auto target = ctx.World->PickCreatureByView(camera->GetPosition(),
                                                      camera->GetFront(), 8.0f);
    std::string error;
    if (target && ctx.World->TryApplySkin(*target, active->id, &error))
    {
      return;
    }
    if (ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage(
          error.empty() ? "No creature in view" : error, 2.0);
    }
  }
}

void UBlockInputController::TryInstantBreak(const BlockInputContext &ctx)
{
  if (ctx.World)
  {
    ctx.World->CancelBreakSession();
    ctx.World->DelObjectByView();
  }
}

void UBlockInputController::HandleLeftPress(const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    return;
  }
  LeftDownTime = std::chrono::steady_clock::now();
  LeftHeld = true;

  if (ctx.Ui->controlScheme == ControlScheme::Classic)
  {
    if (ActiveSlotBlocksWorldInteraction(ctx))
    {
      return;
    }
    if (ctx.World->GetIsBlockIntersectionExists())
    {
      ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
    }
  }
}

void UBlockInputController::HandleLeftRelease(float holdSeconds,
                                              const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    LeftHeld = false;
    return;
  }

  const InventoryEntryRef *active = GetActiveEntry(ctx);

  if (ctx.Ui->controlScheme == ControlScheme::Cubatarium)
  {
    if (active && active->kind == InventoryEntryKind::UCreature &&
        !active->id.empty())
    {
      TrySpawnCreatureOrSkin(ctx);
      LeftHeld = false;
      return;
    }
    if (active && active->kind == InventoryEntryKind::Skin &&
        !active->id.empty())
    {
      TrySpawnCreatureOrSkin(ctx);
      LeftHeld = false;
      return;
    }

    // Cubatarium dead zone: placeClickMaxSeconds <= hold < breakHoldMinSeconds
    // => noop.
    const float placeMax = ctx.Ui->placeClickMaxSeconds;
    const float breakMin = ctx.Ui->breakHoldMinSeconds;

    if (holdSeconds < placeMax)
    {
      ctx.World->CancelBreakSession();
      TryPlaceFromActiveSlot(ctx);
    }
    else if (holdSeconds < breakMin)
    {
      ctx.World->CancelBreakSession();
    }
    else
    {
      if (!ctx.World->HasBreakSession() &&
          ctx.World->GetIsBlockIntersectionExists())
      {
        ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
      }
    }
    LeftHeld = false;
    return;
  }

  // Classic
  if (ActiveSlotBlocksWorldInteraction(ctx))
  {
    TrySpawnCreatureOrSkin(ctx);
    ctx.World->CancelBreakSession();
    LeftHeld = false;
    return;
  }

  if (ctx.World->HasBreakSession())
  {
    ctx.World->CancelBreakSession();
  }
  LeftHeld = false;
}

void UBlockInputController::HandleRightPress(glm::vec2 pos,
                                             const BlockInputContext &ctx)
{
  RightDownPos = pos;
  RightPressed = true;
  RightDragExceeded = false;
  RightLookActive = false;

  if (!ctx.Ui || ctx.Ui->controlScheme != ControlScheme::Cubatarium)
  {
    return;
  }

  if (ctx.World)
  {
    if (auto camera = ctx.World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(pos.x, pos.y);
    }
  }
}

void UBlockInputController::HandleRightRelease(const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    RightPressed = false;
    RightLookActive = false;
    return;
  }

  RightPressed = false;
  RightLookActive = false;

  if (ctx.Ui->controlScheme == ControlScheme::Cubatarium)
  {
    return;
  }

  if (ActiveSlotBlocksWorldInteraction(ctx))
  {
    return;
  }

  TryPlaceFromActiveSlot(ctx);
}

void UBlockInputController::OnMouseButton(MouseButton button, bool pressed,
                                          glm::vec2 pos,
                                          const BlockInputContext &ctx)
{
  if (button == MouseButton::Right)
  {
    if (pressed)
    {
      HandleRightPress(pos, ctx);
      if (ctx.Ui && ctx.Ui->controlScheme == ControlScheme::Cubatarium)
      {
        RightLookActive = true;
      }
    }
    else
    {
      HandleRightRelease(ctx);
    }
    return;
  }

  if (button != MouseButton::Left)
  {
    return;
  }

  if (pressed)
  {
    HandleLeftPress(ctx);
  }
  else
  {
    const double holdSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      LeftDownTime)
            .count();
    HandleLeftRelease(static_cast<float>(holdSeconds), ctx);
  }
}

void UBlockInputController::OnMouseMove(glm::vec2 pos, glm::vec2 delta,
                                        const BlockInputContext &ctx)
{
  (void)delta;
  if (!ctx.World || !ctx.Ui)
  {
    return;
  }

  if (ctx.Ui->controlScheme == ControlScheme::Classic)
  {
    if (auto camera = ctx.World->GetCurrentUserCamera())
    {
      camera->UpdateMouseMove(ctx.World, pos.x, pos.y);
    }
    return;
  }

  if (!RightPressed)
  {
    return;
  }

  const int threshold = ctx.Ui->rmbDragThresholdPx;
  if (!RightDragExceeded &&
      CursorDragDistancePx(pos, RightDownPos) > static_cast<float>(threshold))
  {
    RightDragExceeded = true;
  }

  if (!RightLookActive)
  {
    return;
  }

  if (auto camera = ctx.World->GetCurrentUserCamera())
  {
    camera->UpdateMouseMove(ctx.World, pos.x, pos.y);
  }
}

void UBlockInputController::OnKeyDelete(const BlockInputContext &ctx)
{
  TryInstantBreak(ctx);
}

void UBlockInputController::Tick(float dt, const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    return;
  }

  if (ctx.World->HasBreakSession())
  {
    ctx.World->TickBreakSession(dt, ctx.Ui->breakDurationSeconds);
    if (ctx.World->GetBreakProgress() >= 1.0f)
    {
      ctx.World->CompleteBreakSession();
      LeftHeld = false;
    }
    return;
  }

  if (!LeftHeld || ActiveSlotBlocksWorldInteraction(ctx))
  {
    return;
  }

  if (!ctx.World->GetIsBlockIntersectionExists())
  {
    return;
  }

  if (ctx.Ui->controlScheme == ControlScheme::Cubatarium)
  {
    const float holdSeconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                     LeftDownTime)
            .count();
    if (holdSeconds >= ctx.Ui->breakHoldMinSeconds)
    {
      ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
    }
    return;
  }

  ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
}

} // namespace cutum
