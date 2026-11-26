#pragma once

#include "Game/GameSession.h"
#include "Gui/Core/GuiKeyCodes.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include <fstream>

namespace cutum
{
inline nlohmann::json CheckConsoleRequests(const std::filesystem::path &report)
{
  using gui_context_fixture::Require;
  auto path = report;
  path += ".history-fixture.txt";
  Require(!std::filesystem::exists(path), "console history fixture must be fresh");
  {
    std::ofstream file(path, std::ios::binary);
    const std::string tail(400, 'Z');
    for (int i = 0; i < 350; ++i)
      file << "entry-" << i << ' ' << tail << "\r\n";
    file << std::string(1048576, '\1') << "\tlast\tcontinued " << std::string(1048576, 'v');
    Require(bool(file), "write bounded history fixture input");
  }
  { UConsoleCommandHistory warm; warm.SetFilePath(path); Require(warm.Load(), "warm actual history reader"); }
  UGuiContext context;
  auto shaders = std::make_shared<UShaderManager>();
  Require(context.Initialize(shaders, {}), "console fixture initializes renderer");
  auto session = std::make_unique<UGameSession>(nullptr, nullptr);
  const std::string draft(200, 'd');
  const std::string line(120, 'L');
  ConsoleStorage loaded, visible, open, closed;
  size_t history_peak = 0, history_max_request = 0, refusal_count = 0;
  {
    observation::Scope scope;
    auto popup = std::make_unique<UGuiPopupMenu>(&context.GetTheme());
    auto console = std::make_unique<UConsoleScreen>(session.get());
    console->OnAttach(context);
    console->Build(context);
    console->AttachPopup(popup.get());
    const auto observe = [&]() {
      const auto s = console->InspectOwnedStorage(context, session.get(), popup.get());
      Require(s.supported && s.borrowed_log && s.requested == observation::State.live,
              "console source matches independent ordinary ownership allocations");
      return s;
    };
    auto &history = session->GetCommandHistory();
    history.SetFilePath(path);
    const auto before_load = observation::State.attempts;
    Require(history.Load(), "load large command history fixture");
    const auto load_attempts = observation::State.attempts - before_load;
    loaded = observe();
    history_peak = observation::State.peak;
    history_max_request = observation::State.max_request;
    Require(history.Entries().size() == 100 && history.Entries().capacity() == 100 &&
                history.Entries().front().compare(0, 10, "entry-251 ") == 0 &&
                history.Entries().back().compare(0, 15, "last continued ") == 0 &&
                history.Entries().back().size() == 256 && history_max_request <= 8192,
            "streaming load preserves last commands and caps each sanitized line");
    for (const auto prefix : {size_t{0}, load_attempts / 2, load_attempts - 1})
    {
      observation::State.fail_after = observation::State.attempts + prefix;
      bool refused = false;
      try { history.Load(); }
      catch (const std::bad_alloc &) { refused = true; }
      Require(refused && observe().requested == loaded.requested &&
                  history.Entries().front().compare(0, 10, "entry-251 ") == 0,
              "failed history reload leaves the previous backing and records intact");
      ++refusal_count;
      observation::State.fail_after = SIZE_MAX;
      Require(history.Load() && observe().requested == loaded.requested,
              "same history reload succeeds after allocation refusal");
    }
    auto &log_view = static_cast<UGuiListView &>(*console->GetRoot()->GetChildren()[0]);
    Require(!log_view.CanFocus(), "borrowed log starts empty");
    for (int i = 0; i < 220; ++i)
      session->AddChatLine(std::to_string(i) + line);
    Require(log_view.CanFocus(), "log view follows real session growth without copying");
    console->OnViewportChanged(1280, 720);
    console->SetVisible(true);
    visible = observe();
    Require(visible.log_rows == 200 && visible.log_text > 24000, "session owns a full retained log");
    const auto before_update = observation::State.attempts;
    observation::State.fail_after = before_update;
    console->Update(0);
    console->Update(0);
    Require(observe().requested == visible.requested && observation::State.attempts == before_update,
            "visible console refresh allocates zero ordinary storage");
    observation::State.fail_after = SIZE_MAX;

    auto &input = static_cast<UGuiTextInput &>(*console->GetRoot()->GetChildren()[1]);
    input.SetText(draft);
    Require(console->RouteKey({GuiKey::Up}) && input.GetText() == history.Entries().back() &&
                console->RouteKey({GuiKey::Down}) && input.GetText() == draft,
            "history browsing returns to the actual edited draft");
    GuiMouseEvent event;
    event.Button = GuiMouseButton::Right;
    event.Pressed = true;
    event.X = input.GetBounds().X + 2;
    event.Y = input.GetBounds().Y + 2;
    Require(console->RouteMouseButton(event, context.GetRenderer()), "console context menu opens");
    open = observe();
    Require(open.popup_open && open.popup_entries == 4 && open.input_text && open.draft_text &&
                !open.callback_bytes, "console retains known popup actions and input storage");
    event.Button = GuiMouseButton::Left;
    event.X = popup->GetBounds().X + 2;
    event.Y = popup->GetBounds().Y + popup->GetBounds().H - 1;
    Require(console->RouteMouseButton(event, context.GetRenderer()) && input.HasSelection(),
            "popup select-all reaches the actual input");
    console->SetVisible(false);
    closed = observe();
    Require(!closed.visible && !closed.popup_open && closed.popup_entries == 4 &&
                closed.requested == open.requested,
            "closed console retains its owned draft and popup backing");
    popup->SetItems({{"unknown", [] {}}});
    Require(!console->InspectOwnedStorage(context, session.get(), popup.get()).supported,
            "foreign popup items reject the complete console profile");
    popup.reset();
    console.reset();
    session.reset();
    Require(!observation::State.live && !observation::State.blocks, "console and session backing retire");
  }
  return {{"status", "PASS"}, {"loaded", ConsoleStorageJson(loaded)}, {"visible", ConsoleStorageJson(visible)},
          {"popup", ConsoleStorageJson(open)}, {"closed", ConsoleStorageJson(closed)},
          {"history_peak", history_peak}, {"history_max_request", history_max_request},
          {"reload_refusal_cases", refusal_count}, {"visible_update_allocate0", true},
          {"unknown_popup_rejected", true}, {"retired", true}};
}
} // namespace cutum
