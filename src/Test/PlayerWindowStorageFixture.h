#pragma once

#include "Game/GameSession.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Screens/PlayerWindowScreen.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"

namespace cutum
{
inline nlohmann::json CheckPlayerWindowRequests()
{
  using gui_context_fixture::Require;
  UGuiContext context;
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}),
          "player window fixture initializes the renderer");
  UGameSession session(nullptr, nullptr);
  UCharacterSheetScreen character(nullptr, nullptr);
  UCreativePaletteScreen catalog(&session.GetContentCatalog(), &session, nullptr, nullptr);
  character.OnAttach(context);
  character.Build(context);
  catalog.OnAttach(context);
  catalog.Build(context);

  const auto build = [&] {
    auto window = std::make_unique<UPlayerWindowScreen>(&session, &character, &catalog, nullptr);
    window->OnAttach(context);
    window->Build(context);
    return window;
  };
  const auto inspect = [&](const UPlayerWindowScreen &window) {
    return window.InspectOwnedStorage(context, &session, &character, &catalog, nullptr);
  };
  auto window = build(); // Warm borrowed pages before observing the chrome allocations.
  PlayerWindowStorage cold, updated;
  size_t buildAttempts = 0;
  {
    observation::Scope epoch;
    auto replacement = build();
    buildAttempts = observation::State.attempts;
    cold = inspect(*replacement);
    Require(cold.supported && cold.widgets == 14 && cold.callbacks == 21 && !cold.callback_bytes &&
                cold.requested == observation::State.live,
            "player window shell and owned tree match independent allocation observations");
    Require(!replacement->InspectOwnedStorage(context, &session, nullptr, &catalog, nullptr).supported,
            "player window rejects a missing borrowed character page");
    replacement->Update(0);
    updated = inspect(*replacement);
    Require(updated.supported && updated.text_bytes > cold.text_bytes &&
                updated.requested == observation::State.live,
            "player window title backing counted after initial update");
    const auto before = observation::State.attempts;
    observation::State.fail_after = before;
    for (size_t i = 0; i < 12; ++i)
    {
      replacement->SelectSection(0);
      replacement->Update(0);
      const auto bounds = replacement->GetHotbarBounds(i % 10);
      SlotAddress slot;
      Require(replacement->PickHotbarSlot(bounds.X + 1, bounds.Y + 1, slot) &&
                  slot.surface == SlotSurface::Hotbar && slot.bar == 0 && slot.slot == i % 10,
              "player window hit testing returns the targeted slot on the active row");
    }
    const auto passive = inspect(*replacement);
    Require(passive.supported && observation::State.attempts == before &&
                passive.requested == updated.requested,
            "quiet player window update, reroute and census allocate nothing");
    observation::State.fail_after = SIZE_MAX;

    auto &slot = static_cast<UGuiSlot &>(*replacement->GetRoot()->GetChildren()[0]->GetChildren()[2]);
    slot.SetOnClick([padding = std::array<char, 1024>{}] { (void)padding; });
    Require(!inspect(*replacement).supported, "player window rejects an opaque slot action");
    replacement.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "player chrome, title and callback allocations retire together");
  }

  size_t rebuildAttempts{};
  {
    observation::Scope epoch;
    window->Build(context);
    rebuildAttempts = observation::State.attempts;
    const auto rebuilt = inspect(*window);
    Require(rebuilt.supported && rebuilt.requested - rebuilt.screen_bytes == observation::State.live,
            "replacement chrome matches ordinary allocation counts excluding the preexisting screen shell");
    window.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "replacement chrome retires all observed requests");
  }
  for (size_t prefix = 0; prefix < rebuildAttempts; ++prefix)
  {
    window = build();
    const auto *previousRoot = window->GetRoot();
    observation::Scope epoch(prefix);
    bool refused = false;
    try { window->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    const auto intact = inspect(*window);
    Require(refused && intact.supported && window->GetRoot() == previousRoot &&
                intact.requested == cold.requested && !observation::State.live,
            "refused chrome rebuild preserves the published tree and borrowed page binding");
    window->Build(context);
    const auto retry = inspect(*window);
    Require(retry.supported && retry.requested - retry.screen_bytes == observation::State.live,
            "same chrome builds after allocation refusal");
    window.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "player window rebuild and retry retire all requests");
  }

  return {{"status", "PASS"}, {"cold", PlayerWindowStorageJson(cold)},
          {"updated", PlayerWindowStorageJson(updated)}, {"build_attempts", buildAttempts},
          {"rebuild_refusal_prefixes", rebuildAttempts}, {"quiet_allocate0", true},
          {"opaque_action_rejected", true}, {"retired", true}};
}
}
