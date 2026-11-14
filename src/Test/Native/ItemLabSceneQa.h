#pragma once
#include "Gui/Core/GuiContext.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Items/ItemInspectionSource.h"
#include "Test/Native/PlayerWindowCapture.h"
#include <nlohmann/json.hpp>

namespace cutum
{
inline void RunItemLabSceneQa(UItemLabScreen &lab, UGuiContext &gui,
                              const std::filesystem::path &frames,
                              nlohmann::json &report)
{
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  for (int scene = 0; scene < 6; ++scene)
  {
    const std::string id = scene == 4 ? "iron_head" : "iron_pickaxe";
    lab.SelectItem(id);
    lab.SetScene(scene);
    lab.SetMode(0);
    for (int actor = 0; actor < 4; ++actor)
    {
      lab.SetActor(actor);
      lab.Update(0);
      const auto label =
          "scene_" + std::to_string(scene) + "_actor_" + std::to_string(actor);
      for (int panel = 0; panel < 2; ++panel)
        check(label + " source " + std::to_string(panel),
              lab.GetPreviewTexture(panel) &&
                  lab.GetSceneDrawCount(panel) > 0 &&
                  lab.SceneSourceMatches(panel));
      check(label + " capture",
            CapturePlayerWindow(gui, *lab.GetRoot(), lab.GetViewportWidth(),
                                lab.GetViewportHeight(),
                                frames / (label + ".png")));
      check(label + " scope restored", FindItemInspectionSource(id) == nullptr);
    }
  }
  for (int scene = 0; scene < 6; ++scene)
  {
    lab.SelectItem(scene == 4 ? "iron_head" : "iron_pickaxe");
    lab.SetScene(scene);
    lab.SetActor(0);
    for (int mode = 1; mode < 7; ++mode)
    {
      lab.SetMode(mode);
      lab.Update(0);
      const auto label =
          "scene_" + std::to_string(scene) + "_mode_" + std::to_string(mode);
      check(label + " candidate", lab.HasCandidate() &&
                                      lab.SceneSourceMatches(1) &&
                                      lab.GetSceneDrawCount(1) > 0);
      check(label + " capture",
            CapturePlayerWindow(gui, *lab.GetRoot(), lab.GetViewportWidth(),
                                lab.GetViewportHeight(),
                                frames / (label + ".png")));
    }
  }
  lab.SetMode(0);
  lab.SelectItem("bucket");
  lab.SetScene(4);
  lab.Update(0);
  check("non-armor does not fabricate a worn preview", !lab.HasCandidate());
  for (const char *id : {"copper_legs", "gold_feet"})
  {
    lab.SelectItem(id);
    lab.Update(0);
    check(std::string(id) + " anatomical sections used",
          lab.HasCandidate() && lab.GetSceneDrawCount(1) > 1 &&
              lab.SceneSourceMatches(1));
  }
  lab.SelectItem("bucket");
  lab.SetActor(0);
  lab.SetScene(0);
  lab.SetMode(0);
  lab.Update(0);
}
} // namespace cutum
