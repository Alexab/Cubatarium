#include "Blocks/Input/BlockInputController.h"

#include "App/Application.h"
#if defined(__ANDROID__)
#include "App/Platform/TouchInputBridge.h"
#endif
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"

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

bool ShouldBlockBreakByMovement(const BlockInputContext &ctx)
{
#if defined(__ANDROID__)
  if (!ctx.App)
  {
    return false;
  }
  const auto *touch = ctx.App->GetTouchInputBridge();
  return touch && touch->IsMovementBlockingBreak();
#else
  (void)ctx;
  return false;
#endif
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

void UBlockInputController::TryUseActiveSlot(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return;
  }
  const InventoryEntryRef *Active = GetActiveEntry(ctx);
  if (!Active || Active->empty || Active->Id.empty())
  {
    return;
  }
  switch (Active->kind)
  {
  case InventoryEntryKind::UObject:
    ctx.World->PlaceActivePrefabByView();
    break;
  case InventoryEntryKind::UCreature:
    if (!ctx.World->SpawnCreatureByView(Active->Id) && ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage("Cannot spawn " + Active->Id, 2.0);
    }
    break;
  case InventoryEntryKind::Skin:
  {
    auto camera = ctx.World->GetCurrentUserCamera();
    if (!camera)
    {
      return;
    }
    const auto target = ctx.World->PickCreatureByView(camera->GetPosition(),
                                                    camera->GetFront(), 8.0f);
    std::string error;
    if (target && ctx.World->TryApplySkin(*target, Active->Id, &error))
    {
      return;
    }
    if (ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage(
          error.empty() ? "No creature in view" : error, 2.0);
    }
    break;
  }
  case InventoryEntryKind::Block:
  default:
    ctx.World->AddObjectByView();
    break;
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

void UBlockInputController::CancelPointerInteraction(
    const BlockInputContext &ctx)
{
  LeftHeld = false;
  if (ctx.World)
  {
    ctx.World->CancelBreakSession();
  }
}

void UBlockInputController::OnQuickTap(const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    return;
  }
  // Touch: Classic = RMB place/use; Cubatarium = LMB short tap place/use.
  ctx.World->CancelBreakSession();
  TryUseActiveSlot(ctx);
  LeftHeld = false;
}

void UBlockInputController::HandleLeftPress(const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    return;
  }
  LeftDownTime = std::chrono::steady_clock::now();
  LeftHeld = true;

  // Classic: LMB down starts break immediately (independent of hotbar slot).
  if (ctx.Ui->ControlScheme == ControlScheme::Classic &&
      ctx.World->GetIsBlockIntersectionExists())
  {
    ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
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

  if (ctx.Ui->ControlScheme == ControlScheme::Cubatarium)
  {
    const float placeMax = ctx.Ui->PlaceClickMaxSeconds;
    const float breakMin = ctx.Ui->BreakHoldMinSeconds;

    if (holdSeconds < placeMax)
    {
      // Short tap: place/use active slot (block, prefab, creature, skin).
      ctx.World->CancelBreakSession();
      TryUseActiveSlot(ctx);
    }
    else if (holdSeconds < breakMin)
    {
      // Dead zone: no place, no break.
      ctx.World->CancelBreakSession();
    }
    else if (!ctx.World->HasBreakSession() &&
             ctx.World->GetIsBlockIntersectionExists() &&
             !ShouldBlockBreakByMovement(ctx))
    {
      // Long press release without Tick having started break yet.
      ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
    }
    LeftHeld = false;
    return;
  }

  // Classic: release ends break if still in progress (completed breaks already cleared).
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

  if (!ctx.Ui || ctx.Ui->ControlScheme != ControlScheme::Cubatarium)
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

  // Cubatarium: RMB is look only.
  if (ctx.Ui->ControlScheme == ControlScheme::Cubatarium)
  {
    return;
  }

  // Classic: RMB click places/uses active slot (independent of LMB break).
  TryUseActiveSlot(ctx);
}

void UBlockInputController::OnMouseButton(MouseButton Button, bool Pressed,
                                          glm::vec2 pos,
                                          const BlockInputContext &ctx)
{
  if (Button == MouseButton::Right)
  {
    if (Pressed)
    {
      HandleRightPress(pos, ctx);
      if (ctx.Ui && ctx.Ui->ControlScheme == ControlScheme::Cubatarium)
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

  if (Button != MouseButton::Left)
  {
    return;
  }

  if (Pressed)
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

  if (ctx.Ui->ControlScheme == ControlScheme::Classic)
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

  const int threshold = ctx.Ui->RmbDragThresholdPx;
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
    ctx.World->TickBreakSession(dt, ctx.Ui->BreakDurationSeconds);
    if (ctx.World->GetBreakProgress() >= 1.0f)
    {
      ctx.World->CompleteBreakSession();
      LeftHeld = false;
    }
    return;
  }

  if (!LeftHeld || !ctx.World->GetIsBlockIntersectionExists())
  {
    return;
  }

  // Cubatarium: hold LMB past breakMin to start break (any hotbar slot).
  if (ctx.Ui->ControlScheme == ControlScheme::Cubatarium)
  {
    if (ShouldBlockBreakByMovement(ctx))
    {
      return;
    }
    const float holdSeconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                     LeftDownTime)
            .count();
    if (holdSeconds >= ctx.Ui->BreakHoldMinSeconds)
    {
      ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
    }
    return;
  }

  // Classic: break already started on press; Tick is a fallback if needed.
  ctx.World->StartBreakSession(ctx.World->GetBreakBlockPos());
}

} // namespace cutum
