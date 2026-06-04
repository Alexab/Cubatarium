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

namespace cutum {

namespace {

float CursorDragDistancePx(glm::vec2 a, glm::vec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

bool BlockInputController::IsAltDown(const BlockInputContext& ctx) const
{
    return ctx.window
        && (glfwGetKey(ctx.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
            || glfwGetKey(ctx.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
}

const InventoryEntryRef* BlockInputController::GetActiveEntry(const BlockInputContext& ctx) const
{
    if (!ctx.world) {
        return nullptr;
    }
    if (Creature* controlled = ctx.world->GetControlledCreature()) {
        return controlled->GetInventory().GetActiveEntryRef();
    }
    return nullptr;
}

bool BlockInputController::ActiveSlotBlocksWorldInteraction(const BlockInputContext& ctx) const
{
    const InventoryEntryRef* active = GetActiveEntry(ctx);
    if (!active) {
        return false;
    }
    if (active->kind == InventoryEntryKind::Creature && !active->id.empty()) {
        return true;
    }
    if (active->kind == InventoryEntryKind::Skin && !active->id.empty()) {
        return true;
    }
    return false;
}

void BlockInputController::TryPlaceBlockOrPrefab(bool altDown, const BlockInputContext& ctx)
{
    if (!ctx.world) {
        return;
    }
    const InventoryEntryRef* active = GetActiveEntry(ctx);
    const bool placePrefab =
        altDown || (active && active->kind == InventoryEntryKind::Object && !active->id.empty());
    if (placePrefab) {
        ctx.world->PlaceActivePrefabByView();
    } else {
        ctx.world->AddObjectByView();
    }
}

void BlockInputController::TrySpawnCreatureOrSkin(const BlockInputContext& ctx)
{
    if (!ctx.world) {
        return;
    }
    const InventoryEntryRef* active = GetActiveEntry(ctx);
    if (!active) {
        return;
    }
    if (active->kind == InventoryEntryKind::Creature && !active->id.empty()) {
        if (!ctx.world->SpawnCreatureByView(active->id) && ctx.geometries) {
            ctx.geometries->ShowTransientMessage("Cannot spawn " + active->id, 2.0);
        }
        return;
    }
    if (active->kind == InventoryEntryKind::Skin && !active->id.empty()) {
        auto camera = ctx.world->GetCurrentUserCamera();
        if (!camera) {
            return;
        }
        const auto target = ctx.world->PickCreatureByView(
            camera->GetPosition(), camera->GetFront(), 8.0f);
        std::string error;
        if (target && ctx.world->TryApplySkin(*target, active->id, &error)) {
            return;
        }
        if (ctx.geometries) {
            ctx.geometries->ShowTransientMessage(
                error.empty() ? "No creature in view" : error, 2.0);
        }
    }
}

void BlockInputController::TryInstantBreak(const BlockInputContext& ctx)
{
    if (ctx.world) {
        ctx.world->CancelBreakSession();
        ctx.world->DelObjectByView();
    }
}

void BlockInputController::HandleLeftPress(const BlockInputContext& ctx)
{
    if (!ctx.ui || !ctx.world) {
        return;
    }
    leftDownTime_ = std::chrono::steady_clock::now();
    leftHeld_ = true;

    if (ctx.ui->blockInputProfile == BlockInputProfile::Classic) {
        if (ActiveSlotBlocksWorldInteraction(ctx)) {
            return;
        }
        if (ctx.world->GetIsBlockIntersectionExists()) {
            ctx.world->StartBreakSession(ctx.world->GetBreakBlockPos());
        }
    }
}

void BlockInputController::HandleLeftRelease(float holdSeconds, const BlockInputContext& ctx)
{
    if (!ctx.ui || !ctx.world) {
        leftHeld_ = false;
        return;
    }

    const InventoryEntryRef* active = GetActiveEntry(ctx);

    if (ctx.ui->blockInputProfile == BlockInputProfile::Cubatarium) {
        if (active && active->kind == InventoryEntryKind::Creature && !active->id.empty()) {
            TrySpawnCreatureOrSkin(ctx);
            leftHeld_ = false;
            return;
        }
        if (active && active->kind == InventoryEntryKind::Skin && !active->id.empty()) {
            TrySpawnCreatureOrSkin(ctx);
            leftHeld_ = false;
            return;
        }

        // Cubatarium dead zone: placeClickMaxSeconds <= hold < breakHoldMinSeconds => noop.
        const float placeMax = ctx.ui->placeClickMaxSeconds;
        const float breakMin = ctx.ui->breakHoldMinSeconds;

        if (holdSeconds < placeMax) {
            ctx.world->CancelBreakSession();
            TryPlaceBlockOrPrefab(IsAltDown(ctx), ctx);
        } else if (holdSeconds < breakMin) {
            ctx.world->CancelBreakSession();
        } else {
            if (!ctx.world->HasBreakSession()
                && ctx.world->GetIsBlockIntersectionExists()) {
                ctx.world->StartBreakSession(ctx.world->GetBreakBlockPos());
            }
        }
        leftHeld_ = false;
        return;
    }

    // Classic
    if (ActiveSlotBlocksWorldInteraction(ctx)) {
        TrySpawnCreatureOrSkin(ctx);
        ctx.world->CancelBreakSession();
        leftHeld_ = false;
        return;
    }

    if (ctx.world->HasBreakSession()) {
        ctx.world->CancelBreakSession();
    }
    leftHeld_ = false;
}

void BlockInputController::HandleRightPress(glm::vec2 pos, const BlockInputContext& ctx)
{
    rightDownPos_ = pos;
    rightPressed_ = true;
    rightDragExceeded_ = false;
    rightLookActive_ = false;

    if (ctx.world) {
        if (auto camera = ctx.world->GetCurrentUserCamera()) {
            camera->ResetMouseMove(pos.x, pos.y);
        }
    }
}

void BlockInputController::HandleRightRelease(const BlockInputContext& ctx)
{
    if (!ctx.ui || !ctx.world) {
        rightPressed_ = false;
        rightLookActive_ = false;
        return;
    }

    const bool wasLookOnly =
        rightDragExceeded_
        || (ctx.ui->blockInputProfile == BlockInputProfile::Cubatarium);

    rightPressed_ = false;
    rightLookActive_ = false;

    if (wasLookOnly) {
        return;
    }

    if (ActiveSlotBlocksWorldInteraction(ctx)) {
        return;
    }

    TryPlaceBlockOrPrefab(IsAltDown(ctx), ctx);
}

void BlockInputController::OnMouseButton(
    MouseButton button, bool pressed, glm::vec2 pos, const BlockInputContext& ctx)
{
    if (button == MouseButton::Right) {
        if (pressed) {
            HandleRightPress(pos, ctx);
            rightLookActive_ = true;
        } else {
            HandleRightRelease(ctx);
        }
        return;
    }

    if (button != MouseButton::Left) {
        return;
    }

    if (pressed) {
        HandleLeftPress(ctx);
    } else {
        const double holdSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - leftDownTime_).count();
        HandleLeftRelease(static_cast<float>(holdSeconds), ctx);
    }
}

void BlockInputController::OnMouseMove(glm::vec2 pos, glm::vec2 delta, const BlockInputContext& ctx)
{
    (void)delta;
    if (!rightPressed_ || !ctx.world) {
        return;
    }

    const int threshold = ctx.ui ? ctx.ui->rmbDragThresholdPx : 4;
    if (!rightDragExceeded_
        && CursorDragDistancePx(pos, rightDownPos_) > static_cast<float>(threshold)) {
        rightDragExceeded_ = true;
    }

    if (!rightLookActive_) {
        return;
    }

    if (auto camera = ctx.world->GetCurrentUserCamera()) {
        camera->UpdateMouseMove(ctx.world, pos.x, pos.y);
    }
}

void BlockInputController::OnKeyDelete(const BlockInputContext& ctx)
{
    TryInstantBreak(ctx);
}

void BlockInputController::Tick(float dt, const BlockInputContext& ctx)
{
    if (!ctx.ui || !ctx.world) {
        return;
    }

    if (ctx.world->HasBreakSession()) {
        ctx.world->TickBreakSession(dt, ctx.ui->breakDurationSeconds);
        if (ctx.world->GetBreakProgress() >= 1.0f) {
            ctx.world->CompleteBreakSession();
            leftHeld_ = false;
        }
        return;
    }

    if (!leftHeld_ || ActiveSlotBlocksWorldInteraction(ctx)) {
        return;
    }

    if (!ctx.world->GetIsBlockIntersectionExists()) {
        return;
    }

    if (ctx.ui->blockInputProfile == BlockInputProfile::Cubatarium) {
        const float holdSeconds = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - leftDownTime_).count();
        if (holdSeconds >= ctx.ui->breakHoldMinSeconds) {
            ctx.world->StartBreakSession(ctx.world->GetBreakBlockPos());
        }
        return;
    }

    ctx.world->StartBreakSession(ctx.world->GetBreakBlockPos());
}

} // namespace cutum
