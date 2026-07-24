#include "World/View/WorldViewSettings.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "world_view_settings_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::WorldProjectionMode;
  using cutum::WorldViewSettings;

  {
    const WorldViewSettings defaults;
    Expect(defaults.Projection == WorldProjectionMode::Perspective,
           "default projection perspective");
    Expect(defaults.OrthoSize == 24.0f, "default ortho size");
    Expect(defaults.IsoYawIndex == 0, "default yaw index");
  }

  {
    const nlohmann::json empty = nlohmann::json::object();
    const WorldViewSettings parsed = WorldViewSettings::FromJson(empty);
    Expect(parsed.Projection == WorldProjectionMode::Perspective,
           "empty object → perspective");
  }

  {
    const WorldViewSettings missing =
        WorldViewSettings::FromJson(nlohmann::json());
    Expect(missing.Projection == WorldProjectionMode::Perspective,
           "non-object → perspective");
  }

  {
    WorldViewSettings settings;
    settings.Projection = WorldProjectionMode::OrthographicIsometric;
    settings.OrthoSize = 32.0f;
    settings.IsoYawIndex = 2;
    settings.IsoPitchDeg = 30.0f;
    const nlohmann::json dumped = settings.ToJson();
    const WorldViewSettings round_trip = WorldViewSettings::FromJson(dumped);
    Expect(round_trip.Projection == WorldProjectionMode::OrthographicIsometric,
           "round-trip isometric");
    Expect(std::abs(round_trip.OrthoSize - 32.0f) < 1e-4f,
           "round-trip ortho size");
    Expect(round_trip.IsoYawIndex == 2, "round-trip yaw index");
    Expect(std::abs(round_trip.IsoPitchDeg - 30.0f) < 1e-4f,
           "round-trip pitch");
  }

  {
    const nlohmann::json alias =
        nlohmann::json{{"projection", "isometric"}, {"ortho_size", 10.0f}};
    const WorldViewSettings parsed = WorldViewSettings::FromJson(alias);
    Expect(parsed.Projection == WorldProjectionMode::OrthographicIsometric,
           "isometric alias");
  }

  {
    WorldViewSettings settings;
    settings.OrthoSize = 1.0f;
    settings.IsoYawIndex = 7;
    settings.IsoPitchDeg = 90.0f;
    settings.Validate();
    Expect(settings.OrthoSize >= 4.0f, "ortho size clamped min");
    Expect(settings.IsoYawIndex == 3, "yaw index mod 4");
    Expect(settings.IsoPitchDeg <= 60.0f, "pitch clamped max");
  }

  {
    WorldProjectionMode mode = WorldProjectionMode::Perspective;
    Expect(cutum::WorldProjectionModeFromString("orthographic_isometric", mode),
           "parse orthographic_isometric");
    Expect(mode == WorldProjectionMode::OrthographicIsometric,
           "parsed mode isometric");
    Expect(std::string(cutum::WorldProjectionModeToString(
               WorldProjectionMode::Perspective)) == "perspective",
           "to_string perspective");
  }

  return 0;
}
