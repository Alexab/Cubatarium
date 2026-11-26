#pragma once

#include "Gui/Widgets/GuiButton.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"

namespace cutum
{
inline nlohmann::json CheckMainMenuRequests(UApplication &app)
{
  using gui_context_fixture::Require;
  using Probe = ApplicationOperationSourceProbe;
  using Json = nlohmann::json;
  auto &context = app.GetGui();
  auto shaders = std::make_shared<UShaderManager>();
  Require(context.Initialize(shaders, {}), "main menu storage fixture initializes");
  Json cases = Json::array();
  for (const bool resume : {false, true})
  {
    Probe::SetMenuSession(app, resume);
    MainMenuStorage ready, modal, retired;
    size_t attempts = 0, peak = 0;
    {
      observation::Scope scope;
      Probe::ShowMain(app);
      ready = app.InspectMainMenuStorage();
      Require(ready.supported && ready.present && ready.resume == resume &&
                  ready.widgets == (resume ? 14 : 13) && ready.callbacks == (resume ? 8 : 7) &&
                  ready.requested == observation::State.live,
              "actual main menu tree matches independent ordinary requests");
      attempts = observation::State.attempts;
      auto &menu = static_cast<UMainMenuScreen &>(*context.GetScreen());
      const auto &children = menu.GetRoot()->GetChildren();
      auto &quit = static_cast<UGuiButton &>(*children[children.size() - 2]);
      Require(quit.Activate(), "menu quit button opens confirmation");
      modal = app.InspectMainMenuStorage();
      Require(modal.supported && modal.quit_dialog && modal.requested == ready.requested &&
                  modal.requested == observation::State.live, "modal shares the retained menu graph");
      auto &cancel = static_cast<UGuiButton &>(*children.back()->GetChildren()[0]->GetChildren()[2]);
      Require(cancel.Activate() && !menu.IsQuitConfirmationVisible(), "menu cancel restores normal navigation");
      const auto before = observation::State.attempts;
      observation::State.fail_after = before;
      const auto passive = app.InspectMainMenuStorage();
      Require(passive.supported && passive.requested == ready.requested &&
                  observation::State.attempts == before, "menu source inspection makes no allocation");
      observation::State.fail_after = SIZE_MAX;
      if (resume)
      {
        auto &primary = static_cast<UGuiButton &>(*children[2]);
        const auto bounds = primary.GetBounds();
        GuiMouseEvent event;
        event.X = bounds.X + bounds.W / 2;
        event.Y = bounds.Y + bounds.H / 2;
        Require(context.RouteMouseDown(event) && context.RouteMouseUp(event),
                "actual resume release is consumed even as the menu retires");
      }
      else
        Probe::DetachMain(app);
      retired = app.InspectMainMenuStorage();
      peak = observation::State.peak;
      Require(retired.supported && !retired.present && !context.GetScreen() &&
                  !observation::State.live && !observation::State.blocks,
              "main menu and its application alias retire");
    }
    Probe::SetMenuSession(app, resume);
    for (size_t n = 0; n < attempts; ++n)
    {
      observation::Scope scope(n);
      bool refused = false;
      try { Probe::ShowMain(app); }
      catch (const std::bad_alloc &) { refused = true; }
      const auto empty = app.InspectMainMenuStorage();
      Require(refused && empty.supported && !empty.present && !context.GetScreen() &&
                  !observation::State.live && context.InspectOwnedStorage().bindings_match,
              "partly built main menu retires without an application alias");
      observation::State.fail_after = SIZE_MAX;
      Probe::ShowMain(app);
      const auto retried = app.InspectMainMenuStorage();
      Require(retried.supported && retried.requested == observation::State.live,
              "same menu can attach after allocation refusal");
      Probe::DetachMain(app);
      Require(!observation::State.live && !observation::State.blocks, "menu retry releases requests");
    }
    cases.push_back({{"ready", MainMenuStorageJson(ready)}, {"dialog", MainMenuStorageJson(modal)},
                     {"retired", MainMenuStorageJson(retired)}, {"peak", peak}, {"refusal_prefixes", attempts}});
  }

  Probe::SetMenuSession(app, false);
  Probe::ShowMain(app);
  auto &menu = static_cast<UMainMenuScreen &>(*context.GetScreen());
  Require(!menu.InspectOwnedStorage(context, nullptr).supported, "missing menu action host rejects");
  const auto &root = menu.GetRoot()->GetChildren();
  auto &primary = static_cast<UGuiButton &>(*root[2]);
  primary.SetOnClick([] {});
  Require(!app.InspectMainMenuStorage().supported, "opaque menu handler rejects");
  root[0]->AddChild(std::make_unique<UGuiWidget>());
  Require(!app.InspectMainMenuStorage().bindings_match, "unexpected menu child rejects");
  Probe::DetachMain(app);

  auto button = std::make_unique<UGuiButton>(&context.GetTheme(), "Retire");
  button->SetBounds({0, 0, 10, 10});
  size_t clicks = 0;
  bool repeated = false;
  button->SetOnClick([&] {
    if (++clicks == 1)
    {
      repeated = button->OnMouseUp({100, 100});
      button.reset();
    }
  });
  auto *pressed = button.get();
  Require(pressed->OnMouseDown({1, 1}) && pressed->OnMouseUp({1, 1}) &&
              !button && clicks == 1 && !repeated,
          "button clears press state before an action releases its owner");
  context.Shutdown();
  return {{"status", "PASS"}, {"cases", cases}, {"opaque_controls", true}, {"release_order", true}};
}
} // namespace cutum
