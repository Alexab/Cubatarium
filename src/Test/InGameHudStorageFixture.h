#pragma once

#include "Game/GameSession.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"

namespace cutum
{
inline nlohmann::json CheckInGameHudRequests()
{
  using gui_context_fixture::Require;
  struct SessionFixture : UGameSession
  {
    SessionFixture() : UGameSession(nullptr, {}) {}
    size_t bars{1}, selected_bar{0}, selected_slot{0};
    bool filled{};
    std::string label = std::string(8300, 'X');
    size_t GetBarCount() const override { return bars; }
    void SelectSlot(size_t bar, size_t slot) override { selected_bar = bar; selected_slot = slot; }
    std::array<HotbarSlotView, 10> GetBarSlots(size_t bar) const override
    {
      std::array<HotbarSlotView, 10> result{};
      if (filled && bar == 3)
      {
        result[3].label = label;
        result[3].CountLabel = "42";
      }
      return result;
    }
  } session;
  UGuiContext context;
  auto shaders = std::make_shared<UShaderManager>();
  Require(context.Initialize(shaders, {}), "HUD fixture initializes renderer");
  UGuiIconSource icons({}, {});
  const auto build = [&]() {
    auto hud = std::make_unique<UInGameHudScreen>(&session, &context.GetTheme(), &icons);
    hud->OnAttach(context);
    hud->Build(context);
    hud->OnViewportChanged(1280, 720);
    return hud;
  };
  const auto observe = [&](const UInGameHudScreen &hud) {
    const auto source = hud.InspectOwnedStorage(context, &session, &icons);
    Require(source.supported && source.requested == observation::State.live,
            "HUD snapshot matches independent ordinary requests");
    return source;
  };
  { auto warm = build(); warm->Update(0); warm->SyncSlotIcons(); }

  InGameHudStorage cold, quiet, expanded, collapsed, rebuilt;
  size_t update_attempts = 0, updated_peak = 0;
  {
    observation::Scope scope;
    auto hud = build();
    cold = observe(*hud);
    const auto before = observation::State.attempts;
    hud->Update(0);
    update_attempts = observation::State.attempts - before;
    quiet = observe(*hud);
    Require(quiet.widgets == 53 && quiet.slots == 40 && quiet.visible_slots == 10 &&
                quiet.callbacks == 80 && !quiet.callback_bytes,
            "all forty actual slots have compact selection and drag handlers");
    const auto steady = observation::State.attempts;
    observation::State.fail_after = steady;
    hud->Update(0);
    hud->SyncSlotIcons();
    Require(observe(*hud).requested == quiet.requested && observation::State.attempts == steady,
            "steady empty HUD update and icon sync allocate zero ordinary storage");
    observation::State.fail_after = SIZE_MAX;

    session.bars = 4;
    session.filled = true;
    auto &slot = static_cast<UGuiSlot &>(*hud->GetRoot()->GetChildren()[33]);
    const auto bounds = slot.GetBounds();
    GuiMouseEvent event;
    event.X = bounds.X + bounds.W / 2;
    event.Y = bounds.Y + bounds.H / 2;
    hud->SetPointerPosition(event.X, event.Y);
    hud->Update(0);
    hud->SyncSlotIcons();
    SlotAddress picked;
    const auto &tip = static_cast<const UGuiLabel &>(*hud->GetRoot()->GetChildren()[40]);
    Require(tip.IsVisible() && tip.GetText() == session.label &&
                hud->PickSlot(event.X, event.Y, picked) && picked.bar == 3 && picked.slot == 3 &&
                slot.OnMouseDown(event) && slot.OnMouseUp(event) &&
                session.selected_bar == 3 && session.selected_slot == 3,
            "secondary tooltip, hit and selection resolve the actual fourth bar");
    expanded = observe(*hud);
    Require(expanded.visible_slots == 40 && expanded.text_bytes > 16000,
            "full HUD counts current slot and tooltip payloads");

    session.bars = 1;
    session.filled = false;
    hud->SetPointerPosition(-1, -1);
    hud->Update(0);
    collapsed = observe(*hud);
    Require(collapsed.visible_slots == 10 && collapsed.widgets == expanded.widgets &&
                collapsed.text_bytes == expanded.text_bytes,
            "hidden slots and cleared text keep their storage until rebuild");
    hud->Build(context);
    const auto cleared = observe(*hud);
    Require(cleared.widgets == 1 && !cleared.slots && !cleared.callbacks &&
                cleared.slot_index_bytes == quiet.slot_index_bytes,
            "rebuild removes all previously published widget aliases");
    hud->Update(0);
    rebuilt = observe(*hud);
    Require(rebuilt.requested == quiet.requested, "rebuilt HUD returns to known quiet storage");
    updated_peak = observation::State.peak;
    hud.reset();
    Require(!observation::State.live && !observation::State.blocks, "HUD destruction retires all requests");
  }
  for (size_t n = 0; n < update_attempts; ++n)
  {
    observation::Scope scope;
    auto hud = build();
    observation::State.fail_after = observation::State.attempts + n;
    bool refused = false;
    try { hud->Update(0); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused, "HUD update reaches the selected refusal prefix");
    observe(*hud);
    observation::State.fail_after = SIZE_MAX;
    hud->Update(0);
    Require(observe(*hud).requested == quiet.requested, "same HUD can retry without duplicate widgets");
    hud.reset();
    Require(!observation::State.live && !observation::State.blocks, "HUD retry retires all requests");
  }
  auto hud = build();
  hud->Update(0);
  Require(!hud->InspectOwnedStorage(context, &session, nullptr).supported, "foreign icons reject HUD composition");
  auto &slot = static_cast<UGuiSlot &>(*hud->GetRoot()->GetChildren()[0]);
  slot.SetOnBeginDrag([] {});
  Require(!hud->InspectOwnedStorage(context, &session, &icons).supported, "opaque slot handler rejects");
  hud.reset();

  size_t legacy_bytes = 0;
  SlotAddress address;
  address.surface = SlotSurface::Hotbar;
  const auto old_capture = [screen = static_cast<UInGameHudScreen *>(nullptr), address] {
    (void)screen;
    (void)address;
  };
  {
    observation::Scope scope;
    std::function<void()> legacy(old_capture);
    legacy_bytes = observation::State.live;
  }
  Require(legacy_bytes && !observation::State.live, "original drag capture shape requested backing");
  return {{"status", "PASS"}, {"cold", InGameHudStorageJson(cold)}, {"quiet", InGameHudStorageJson(quiet)},
          {"expanded", InGameHudStorageJson(expanded)}, {"collapsed", InGameHudStorageJson(collapsed)},
          {"rebuilt", InGameHudStorageJson(rebuilt)}, {"update_peak", updated_peak},
          {"refusal_prefixes", update_attempts}, {"previous_drag_request", legacy_bytes},
          {"steady_empty_allocate0", true}, {"opaque_controls", true}, {"retired", true}};
}
} // namespace cutum
