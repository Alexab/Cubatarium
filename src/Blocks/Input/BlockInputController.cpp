#include "Blocks/Input/BlockInputController.h"
#include "App/Application.h"
#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/WorldGameMode.h"
#include "Game/ModePolicy.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemUseRegistry.h"
#include "Items/ItemVisualDefaults.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Core/World.h"
#include "World/Diagnostics/BlockInspectDiagnostics.h"
#include "World/Raycast/BlockRaycast.h"
#if defined(__ANDROID__)
#include "App/Platform/TouchInputBridge.h"
#endif
#include "App/Platform/GlfwKeyCompat.h"

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

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

bool IsIsoCamera(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return false;
  }
  if (auto camera = ctx.World->GetCurrentUserCamera())
  {
    return camera->IsIsometricProjection();
  }
  return false;
}

bool UsesRmbLook(const BlockInputContext &ctx)
{
  if (!ctx.Ui)
  {
    return false;
  }
  return ctx.Ui->ControlScheme == ControlScheme::Cubatarium || IsIsoCamera(ctx);
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

bool IsCtrlHeld(const BlockInputContext &ctx)
{
#if defined(__ANDROID__)
  (void)ctx;
  return false;
#else
  if (!ctx.Window)
  {
    return false;
  }
  return glfwGetKey(ctx.Window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
         glfwGetKey(ctx.Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
#endif
}

bool TryBlockInspectClick(const BlockInputContext &ctx)
{
  if (!IsCtrlHeld(ctx) || !ctx.World)
  {
    return false;
  }
  const int sample =
      UBlockInspectDiagnostics::CaptureFromCrosshair(*ctx.World, ctx.Geometries);
  if (sample < 0)
  {
    if (ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage("Block inspect: no target", 1.5);
    }
    return true;
  }
  if (ctx.Geometries)
  {
    ctx.Geometries->ShowTransientMessage(
        "Block inspect logged (#" + std::to_string(sample) + ")", 1.5);
  }
  return true;
}

void ClearDigIntent(UWorld &world)
{
  if (UCreature *controlled = world.GetControlledCreature())
  {
    PlayerInteractionRouter::ClearDigIntent(*controlled);
  }
}

void BeginDig(const BlockInputContext &ctx, glm::ivec3 blockPos)
{
  if (!ctx.World)
  {
    return;
  }
  if (UCreature *controlled = ctx.World->GetControlledCreature())
  {
    PlayerInteractionRouter::BeginDigIntent(*controlled, blockPos);
  }
  if (ctx.App)
  {
    ctx.App->NotifyFpSwing(FpSwingKind::Dig);
  }
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

bool TryOpenWorkstationUi(const BlockInputContext &ctx)
{
  if (!ctx.World || !ctx.App)
  {
    return false;
  }
  auto camera = ctx.World->GetCurrentUserCamera();
  if (!camera)
  {
    return false;
  }
  const auto hit = RaycastSolidBlocks(ctx.World->GetBlockWorld(),
                                      ctx.World->GetBlockRegistry(),
                                      camera->GetPosition(), camera->GetFront(),
                                      8.0f);
  if (!hit)
  {
    return false;
  }
  const BlockId id = ctx.World->GetBlockWorld().GetBlock(hit->blockPos);
  const std::string &name = ctx.World->GetBlockRegistry().GetTypeNameById(id);
  if (name == "crafting_table")
  {
    ctx.App->OpenCraftingScreen();
    return true;
  }
  if (name == "anvil" || name == "anvil_slightly_damaged" ||
      name == "anvil_very_damaged")
  {
    ctx.App->OpenAnvilScreen();
    return true;
  }
  return false;
}

void UBlockInputController::TryUseActiveSlot(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return;
  }
  if (TryOpenWorkstationUi(ctx))
  {
    return;
  }
  const InventoryEntryRef *Active = GetActiveEntry(ctx);
  if (!Active || Active->empty || Active->Id.empty())
  {
    return;
  }
  bool placed = false;
  switch (Active->kind)
  {
  case InventoryEntryKind::Object:
    ctx.World->PlaceActiveObjectByView();
    placed = true;
    break;
  case InventoryEntryKind::UCreature:
    if (ctx.World->SpawnCreatureByView(Active->Id))
    {
      placed = true;
    }
    else if (ctx.Geometries)
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
      placed = true;
      break;
    }
    if (ctx.Geometries)
    {
      ctx.Geometries->ShowTransientMessage(
          error.empty() ? "No creature in view" : error, 2.0);
    }
    break;
  }
  case InventoryEntryKind::Item:
  {
    // Consumables (eat/drink) via Use influence channel.
    UCreature *controlled = ctx.World->GetControlledCreature();
    const UItemDefinitionStorage *items = ctx.World->GetItemDefinitionStorage();
    if (controlled && items)
    {
      if (const ItemDefinition *def = items->Get(Active->Id))
      {
        const ItemUseParams use = ItemUseRegistry::FromDefinition(*def);
        if (use.Action == ItemUseAction::Eat ||
            use.Action == ItemUseAction::Drink)
        {
          PlayerInteractionRouter::SetUseIntent(*controlled);
          if (ctx.App)
          {
            const std::string preset =
                DefaultUsePreset(*def, use.Action == ItemUseAction::Drink
                                           ? "drink"
                                           : "eat");
            ctx.App->NotifyFpUseVisual(preset, false);
          }
        }
      }
    }
    break;
  }
  case InventoryEntryKind::Block:
  default:
    ctx.World->AddObjectByView();
    placed = true;
    break;
  }
  if (placed && ctx.App)
  {
    ctx.App->NotifyFpSwing(FpSwingKind::Place);
  }
}

void UBlockInputController::TryInstantBreak(const BlockInputContext &ctx)
{
  if (!ctx.World)
  {
    return;
  }
  if (!ModePolicy::AllowsInstantDelete(ctx.World->GetGameMode()))
  {
    return;
  }
  ctx.World->CancelBreakSession();
  ctx.World->DelObjectByView();
}

void UBlockInputController::CancelPointerInteraction(
    const BlockInputContext &ctx)
{
  LeftHeld = false;
  DigStartedForHold = false;
  if (ctx.World)
  {
    ctx.World->CancelBreakSession();
    ClearDigIntent(*ctx.World);
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
  ClearDigIntent(*ctx.World);
  TryUseActiveSlot(ctx);
  LeftHeld = false;
  DigStartedForHold = false;
}

void UBlockInputController::HandleLeftPress(const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    return;
  }
  DigStartedForHold = false;
  if (TryBlockInspectClick(ctx))
  {
    return;
  }

  // Survival: LMB on a creature → Melee Intent (Router); else dig below.
  if (auto camera = ctx.World->GetCurrentUserCamera())
  {
    if (UCreature *self = ctx.World->GetControlledCreature())
    {
      if (PlayerInteractionRouter::TryRouteMeleeFromView(
              *ctx.World, *self, camera->GetPosition(), camera->GetFront()))
      {
        if (ctx.App)
        {
          ctx.App->NotifyFpSwing(FpSwingKind::Melee);
        }
        LeftDownTime = std::chrono::steady_clock::now();
        LeftHeld = true;
        return;
      }
    }
  }

  LeftDownTime = std::chrono::steady_clock::now();
  LeftHeld = true;

  // Classic perspective: LMB down starts break immediately.
  // Isometric always uses Cubatarium-style hold-to-break (cursor aim).
  if (ctx.Ui->ControlScheme == ControlScheme::Classic && !IsIsoCamera(ctx) &&
      ctx.World->GetIsBlockIntersectionExists())
  {
    BeginDig(ctx, ctx.World->GetBreakBlockPos());
    DigStartedForHold = true;
  }
}

void UBlockInputController::HandleLeftRelease(float holdSeconds,
                                              const BlockInputContext &ctx)
{
  if (!ctx.Ui || !ctx.World)
  {
    LeftHeld = false;
    DigStartedForHold = false;
    return;
  }

  if (IsCtrlHeld(ctx))
  {
    LeftHeld = false;
    DigStartedForHold = false;
    return;
  }

  if (ctx.Ui->ControlScheme == ControlScheme::Cubatarium || IsIsoCamera(ctx))
  {
    const float placeMax = ctx.Ui->PlaceClickMaxSeconds;
    const float breakMin = ctx.Ui->BreakHoldMinSeconds;

    if (holdSeconds < placeMax)
    {
      // Short tap: place/use active slot (block, prefab, creature, skin).
      ctx.World->CancelBreakSession();
      ClearDigIntent(*ctx.World);
      DigStartedForHold = false;
      TryUseActiveSlot(ctx);
    }
    else if (holdSeconds < breakMin)
    {
      // Dead zone: no place, no break.
      ctx.World->CancelBreakSession();
      ClearDigIntent(*ctx.World);
      DigStartedForHold = false;
    }
    else if (!DigStartedForHold &&
             ctx.World->GetIsBlockIntersectionExists() &&
             !ShouldBlockBreakByMovement(ctx))
    {
      // Long press release without Tick having started dig yet.
      BeginDig(ctx, ctx.World->GetBreakBlockPos());
      DigStartedForHold = true;
    }
    // Else: dig already armed — keep Dig intent; WVB finishes the session.
    LeftHeld = false;
    return;
  }

  // Classic: release ends break if still in progress (completed breaks already cleared).
  if (ctx.World->HasBreakSession())
  {
    ctx.World->CancelBreakSession();
  }
  ClearDigIntent(*ctx.World);
  LeftHeld = false;
  DigStartedForHold = false;
}

void UBlockInputController::HandleRightPress(glm::vec2 pos,
                                             const BlockInputContext &ctx)
{
  RightDownPos = pos;
  RightPressed = true;
  RightDragExceeded = false;
  RightLookActive = false;

  if (!UsesRmbLook(ctx))
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

  // RMB is look-only for Cubatarium and isometric.
  if (UsesRmbLook(ctx))
  {
    return;
  }

  // Classic perspective: RMB click places/uses active slot.
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
      if (UsesRmbLook(ctx))
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

  if (IsIsoCamera(ctx))
  {
    if (auto camera = ctx.World->GetCurrentUserCamera())
    {
      if (RightLookActive && RightPressed)
      {
        camera->UpdateMouseMove(ctx.World, pos.x, pos.y);
      }
      else
      {
        camera->UpdatePointerAim(ctx.World, pos.x, pos.y);
      }
    }
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
    (void)dt;
    return;
  }

  if (DigStartedForHold)
  {
    ClearDigIntent(*ctx.World);
    LeftHeld = false;
    DigStartedForHold = false;
    return;
  }

  if (!LeftHeld || !ctx.World->GetIsBlockIntersectionExists())
  {
    return;
  }

  // Iso / Cubatarium: hold LMB past breakMin to start break (any hotbar slot).
  if (ctx.Ui->ControlScheme == ControlScheme::Cubatarium || IsIsoCamera(ctx))
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
      BeginDig(ctx, ctx.World->GetBreakBlockPos());
      DigStartedForHold = true;
    }
    return;
  }

  // Classic: break already started on press; Tick is a fallback if needed.
  BeginDig(ctx, ctx.World->GetBreakBlockPos());
  DigStartedForHold = true;
}

} // namespace cutum
