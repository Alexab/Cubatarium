#pragma once

#include "App/Platform/GlfwKeyCompat.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Screens/WorldProgressScreen.h"
#include "Render/Engine/ShaderManager.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
inline nlohmann::json GuiContextStorageJson(const GuiContextStorage &s)
{
  return {{"status", s.supported ? "PASS" : "UNSUPPORTED"},
          {"context_bytes", s.context_bytes}, {"renderer_bytes", s.renderer_bytes},
          {"router_bytes", s.router_bytes}, {"listeners", s.listeners},
          {"listener_capacity", s.listener_capacity}, {"listener_bytes", s.listener_bytes},
          {"focus_entries", s.focus_entries}, {"focus_bytes", s.focus_bytes},
          {"clip_bytes", s.clip_bytes}, {"vertex_bytes", s.vertex_bytes},
          {"screen_attached", s.screen_attached}, {"root_bound", s.root_bound},
          {"renderer_bound", s.renderer_bound}, {"has_focus", s.has_focus},
          {"bindings_match", s.bindings_match}, {"whole_context_complete", false}};
}

inline nlohmann::json WorldProgressStorageJson(const WorldProgressScreenStorage &s)
{
  return {{"status", s.supported ? "PASS" : "UNSUPPORTED"}, {"bindings_match", s.bindings_match},
          {"screen_bytes", s.screen_bytes}, {"widgets", s.widgets}, {"widget_bytes", s.widget_bytes},
          {"children_bytes", s.children_bytes}, {"text_bytes", s.text_bytes},
          {"callback_present", s.callback_present}, {"callback_supported", s.callback_supported},
          {"callback_bytes", s.callback_bytes}, {"requested", s.requested}};
}

namespace gui_context_fixture
{
inline void Require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

inline void AttachProgressScreen(UGuiContext &context)
{
  auto screen = std::make_unique<UWorldProgressScreen>();
  auto *progress = screen.get();
  context.SetScreen(std::move(screen));
  progress->SetFailureActions([] {}, [] {});
}

inline nlohmann::json RunLifecycle()
{
  UGuiContext context;
  context.SetScreen(nullptr);
  const auto cold = context.InspectOwnedStorage();
  Require(cold.supported && !cold.renderer_bytes && !cold.router_bytes, "cold GUI owns no renderer or router");
  auto shaders = std::make_shared<UShaderManager>();
  Require(context.Initialize(shaders, {}), "GUI fixture initializes actual renderer and shaders");
  AttachProgressScreen(context);
  Require(context.RouteKey({GLFW_KEY_TAB}), "GUI fixture exercises actual focus navigation");
  const auto focused = context.InspectOwnedStorage();
  Require(focused.supported && focused.screen_attached && focused.root_bound &&
              focused.renderer_bound && focused.has_focus && focused.focus_entries == 2,
          "focused progress screen and renderer link to the actual router");
  const auto *active = context.GetScreen();
  Require(!context.Initialize({}, {}) && context.GetScreen() == active &&
              context.InspectOwnedStorage().has_focus,
          "failed renderer initialization preserves the active router and screen");

  struct RejectBuild : UGuiScreenBase
  {
    struct Refused {};
    void Build(UGuiContext &) override { throw Refused{}; }
  };
  bool refused = false;
  try { context.SetScreen(std::make_unique<RejectBuild>()); }
  catch (const RejectBuild::Refused &) { refused = true; }
  const auto failed = context.InspectOwnedStorage();
  Require(refused && failed.supported && !context.GetScreen() && !failed.focus_entries &&
              !failed.has_focus && !failed.root_bound && failed.renderer_bound &&
              failed.focus_bytes == focused.focus_bytes && !context.RouteMouseDown({}),
          "failed screen building leaves routing detached and reports retained focus backing");

  AttachProgressScreen(context);
  Require(context.RouteKey({GLFW_KEY_TAB}), "replacement screen accepts focus");
  context.Shutdown();
  context.Shutdown();
  const auto stopped = context.InspectOwnedStorage();
  Require(stopped.supported && !context.GetScreen() && !stopped.renderer_bytes &&
              !stopped.renderer_bound && !stopped.root_bound && !stopped.has_focus &&
              !stopped.focus_entries && stopped.focus_bytes == focused.focus_bytes &&
              stopped.router_bytes == focused.router_bytes && !context.RouteMouseDown({}),
          "shutdown drops screen and renderer links while the cleared router remains owned");
  Require(context.Initialize(shaders, {}), "GUI renderer and router can restart after shutdown");
  const auto restarted = context.InspectOwnedStorage();
  Require(restarted.supported && restarted.renderer_bound && !restarted.focus_bytes,
          "successful restart installs fresh router backing");
  return {{"status", "PASS"}, {"cold", GuiContextStorageJson(cold)}, {"focused", GuiContextStorageJson(focused)},
          {"failed_screen", GuiContextStorageJson(failed)}, {"shutdown", GuiContextStorageJson(stopped)},
          {"restarted", GuiContextStorageJson(restarted)}};
}
} // namespace gui_context_fixture
} // namespace cutum
