#pragma once
#include "Gui/Screens/ItemLabScreen.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cutum
{
inline bool SaveItemQaTexture(unsigned int texture,
                              const std::filesystem::path &path, int width,
                              int height);
// Real native pairs for human artistic review. Captures and submitted geometry
// are checked here; artistic acceptance must be recorded separately.
inline void RunItemLabArtQa(UItemLabScreen &lab,
                            const std::vector<std::string> &ids,
                            const std::filesystem::path &frames,
                            nlohmann::json &report)
{
  std::filesystem::create_directories(frames);
  auto check = [&](const std::string &name, bool ok)
  {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok)
      report["passed"] = false;
  };
  lab.SetPosePreset(0);
  lab.SetActor(0);
  for (const auto &id : ids)
  {
    lab.SelectItem(id);
    auto capture = [&](const std::string &view)
    {
      lab.Update(0);
      for (int panel = 0; panel < 2; ++panel)
      {
        const auto file =
            id + "-" + view + (panel ? "-candidate.png" : "-reference.png");
        check(file + " real source", lab.GetPreviewTexture(panel) &&
                                         lab.GetSceneDrawCount(panel) > 0 &&
                                         lab.SceneSourceMatches(panel));
        check(file + " capture", SaveItemQaTexture(lab.GetPreviewTexture(panel),
                                                   frames / file, 512, 512));
        report["art_gallery"][id]["frames"].push_back(file);
      }
      report["art_gallery"][id]["reference"] =
          lab.GetReferencePath().generic_string();
      report["art_gallery"][id]["candidate"] =
          lab.GetCandidatePath().generic_string();
      report["art_gallery"][id]["staged_candidate"] = lab.UsesStagedCandidate();
    };
    lab.SetScene(0);
    lab.SetMode(0);
    const char *cameras[] = {"orbit", "front", "back", "side", "top"};
    for (int camera = 0; camera < 5; ++camera)
    {
      lab.SetCamera(camera);
      capture(cameras[camera]);
    }
    lab.SetCamera(0);
    const char *modes[] = {"silhouette", "gray", "albedo"};
    for (int mode = 1; mode <= 3; ++mode)
    {
      lab.SetMode(mode);
      capture(modes[mode - 1]);
    }
    lab.SetMode(0);
    lab.SetScene(5);
    capture("icons-32-48-64");
  }
  lab.SetScene(0);
  lab.SetCamera(0);
  lab.SelectItem("bucket");
  lab.Update(0);
}
} // namespace cutum
