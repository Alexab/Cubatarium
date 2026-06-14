#pragma once

#include "App/Settings/RenderSettings.h"
#include "App/Settings/UiSettings.h"
#include <string>

namespace cutum
{

struct AppSettingsSnapshot
{
  std::string DefaultUser;
  std::string DefaultWorld;
  int RenderDistanceChunks{4};
  bool StreamingEnabled{true};
  bool StepUpEnabled{true};
  bool EntityCollisionEnabled{true};
  RenderSettings Render;
  UiSettings Ui;
};

} // namespace cutum
