#pragma once

#include "App/Settings/RenderSettings.h"
#include "App/Settings/UiSettings.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <string>
#include <vector>

namespace cutum
{

struct AppSettingsSnapshot
{
  std::string DefaultUser;
  std::string DefaultWorld;
  int RenderDistanceChunks{4};
  bool StreamingEnabled{true};
  bool StepUpEnabled{true};
  bool FoliageClimbEnabled{true};
  bool EntityCollisionEnabled{true};
  float ActivityTickHz{20.0f};
  RenderSettings Render;
  UiSettings Ui;
  std::vector<std::string> DefaultResourcePacksEnabled;
  ResourcePackSelection DefaultResourcePacks;
};

} // namespace cutum
