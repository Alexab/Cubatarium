#include "World/View/WorldViewSettings.h"

#include "Render/Camera/IsoViewPreset.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{

namespace
{

constexpr float kMinOrthoSize = 4.0f;
constexpr float kMaxOrthoSize = 256.0f;
constexpr float kMinIsoPitchDeg = 15.0f;
constexpr float kMaxIsoPitchDeg = 60.0f;
constexpr float kDefaultIsoPitchDeg = 35.264f;

} // namespace

const char *WorldProjectionModeToString(WorldProjectionMode mode)
{
  switch (mode)
  {
  case WorldProjectionMode::OrthographicIsometric:
    return "orthographic_isometric";
  case WorldProjectionMode::Perspective:
  default:
    return "perspective";
  }
}

bool WorldProjectionModeFromString(const std::string &value,
                                   WorldProjectionMode &out)
{
  if (value == "orthographic_isometric" || value == "isometric")
  {
    out = WorldProjectionMode::OrthographicIsometric;
    return true;
  }
  if (value == "perspective")
  {
    out = WorldProjectionMode::Perspective;
    return true;
  }
  return false;
}

void WorldViewSettings::Validate()
{
  OrthoSize = std::clamp(OrthoSize, kMinOrthoSize, kMaxOrthoSize);
  IsoYawIndex = ((IsoYawIndex % 4) + 4) % 4;
  if (!std::isfinite(IsoPitchDeg))
  {
    IsoPitchDeg = kDefaultIsoPitchDeg;
  }
  IsoPitchDeg = std::clamp(IsoPitchDeg, kMinIsoPitchDeg, kMaxIsoPitchDeg);
}

WorldViewSettings WorldViewSettings::FromJson(const nlohmann::json &root)
{
  WorldViewSettings settings;
  if (!root.is_object())
  {
    settings.Validate();
    return settings;
  }

  if (root.contains("projection") && root["projection"].is_string())
  {
    WorldProjectionMode mode = WorldProjectionMode::Perspective;
    if (WorldProjectionModeFromString(root["projection"].get<std::string>(),
                                      mode))
    {
      settings.Projection = mode;
    }
  }

  settings.OrthoSize = root.value("ortho_size", settings.OrthoSize);
  settings.IsoYawIndex = root.value("iso_yaw_index", settings.IsoYawIndex);
  settings.IsoPitchDeg = root.value("iso_pitch_deg", settings.IsoPitchDeg);
  if (root.contains("iso_view_preset"))
  {
    const int preset = root.value("iso_view_preset", 1);
    settings.IsoBoomPreset =
        static_cast<IsoViewPreset>(std::clamp(preset, 0, 2));
  }
  settings.Validate();
  return settings;
}

nlohmann::json WorldViewSettings::ToJson() const
{
  WorldViewSettings copy = *this;
  copy.Validate();
  return nlohmann::json{
      {"projection", WorldProjectionModeToString(copy.Projection)},
      {"ortho_size", copy.OrthoSize},
      {"iso_yaw_index", copy.IsoYawIndex},
      {"iso_pitch_deg", copy.IsoPitchDeg},
      {"iso_view_preset", static_cast<int>(copy.IsoBoomPreset)},
  };
}

} // namespace cutum
