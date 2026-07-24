#include "World/Environment/EnvironmentConfig.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr float kMinDayLengthMinutes = 1.0f;
constexpr float kMinPrecipMinutes = 2.0f;
constexpr float kFractionEpsilon = 1e-3f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

} // namespace

EnvironmentCelestialBodySpec
ParseCelestialBodySpec(const nlohmann::json &entry)
{
  EnvironmentCelestialBodySpec body;
  if (!entry.is_object())
  {
    return body;
  }
  body.Id = entry.value("id", body.Id);
  body.Type = entry.value("type", body.Type);
  if (entry.contains("color") && entry["color"].is_array() &&
      entry["color"].size() == 3)
  {
    body.Color = glm::vec3(entry["color"][0].get<float>(),
                           entry["color"][1].get<float>(),
                           entry["color"][2].get<float>());
  }
  body.Intensity = entry.value("intensity", body.Intensity);
  body.AngularSizeDeg = entry.value("angular_size_deg", body.AngularSizeDeg);
  body.OrbitInclinationDeg =
      entry.value("orbit_inclination_deg", body.OrbitInclinationDeg);
  body.OrbitPeriodDays = entry.value("orbit_period_days", body.OrbitPeriodDays);
  body.OrbitPhase = entry.value("orbit_phase", body.OrbitPhase);
  body.OrbitLongitudeDeg =
      entry.value("orbit_longitude_deg", body.OrbitLongitudeDeg);
  body.Intensity = std::max(0.0f, body.Intensity);
  body.AngularSizeDeg = std::max(0.1f, body.AngularSizeDeg);
  body.OrbitPeriodDays = std::max(0.01f, body.OrbitPeriodDays);
  return body;
}

bool TryParseCelestialBodiesArray(
    const nlohmann::json &array,
    std::vector<EnvironmentCelestialBodySpec> &out)
{
  if (!array.is_array())
  {
    return false;
  }
  out.clear();
  std::unordered_set<std::string> seen_ids;
  for (const nlohmann::json &entry : array)
  {
    EnvironmentCelestialBodySpec body = ParseCelestialBodySpec(entry);
    if (body.Id.empty())
    {
      continue;
    }
    if (seen_ids.count(body.Id) > 0)
    {
      std::cerr << "EnvironmentConfig: duplicate celestial id '" << body.Id
                << "' skipped" << std::endl;
      continue;
    }
    seen_ids.insert(body.Id);
    out.push_back(body);
    if (static_cast<int>(out.size()) >= kMaxEnvironmentCelestialBodies)
    {
      if (array.size() > kMaxEnvironmentCelestialBodies)
      {
        std::cerr << "EnvironmentConfig: celestial bodies truncated to "
                  << kMaxEnvironmentCelestialBodies << std::endl;
      }
      break;
    }
  }
  return true;
}

void EnvironmentConfig::Validate()
{
  if (std::abs((WeatherAuto.DryFraction + WeatherAuto.PrecipFraction) - 1.0f) >
      kFractionEpsilon)
  {
    std::cerr << "EnvironmentConfig: dry_fraction + precip_fraction != 1, "
                 "resetting to 0.7/0.3"
              << std::endl;
    WeatherAuto.DryFraction = 0.7f;
    WeatherAuto.PrecipFraction = 0.3f;
  }
  WeatherAuto.DryClearFraction = Clamp01(WeatherAuto.DryClearFraction);
  DayLengthMinutes = std::max(kMinDayLengthMinutes, DayLengthMinutes);
  MinAmbient = std::clamp(MinAmbient, 0.02f, 0.5f);
  CelestialHorizonFade = std::clamp(CelestialHorizonFade, 0.01f, 0.5f);

  WeatherAuto.MinPrecipMinutes = std::max(kMinPrecipMinutes,
                                          WeatherAuto.MinPrecipMinutes);
  const float transition_minutes = WeatherAuto.TransitionSeconds / 60.0f;
  if (WeatherAuto.MinPrecipMinutes < transition_minutes)
  {
    WeatherAuto.MinPrecipMinutes = transition_minutes;
  }
  WeatherAuto.MinDryMinutes = std::max(0.0f, WeatherAuto.MinDryMinutes);
  WeatherAuto.TransitionSeconds = std::max(0.0f, WeatherAuto.TransitionSeconds);
  WeatherAuto.EpisodeMinMinutes =
      std::max(0.1f, WeatherAuto.EpisodeMinMinutes);
  WeatherAuto.EpisodeMaxMinutes =
      std::max(WeatherAuto.EpisodeMinMinutes, WeatherAuto.EpisodeMaxMinutes);
  WeatherAuto.PrecipHeavyFraction = Clamp01(WeatherAuto.PrecipHeavyFraction);
  WeatherAuto.DryFraction = Clamp01(WeatherAuto.DryFraction);
  WeatherAuto.PrecipFraction = Clamp01(WeatherAuto.PrecipFraction);

  if (static_cast<int>(CelestialBodies.size()) > kMaxEnvironmentCelestialBodies)
  {
    CelestialBodies.resize(kMaxEnvironmentCelestialBodies);
  }
}

void EnvironmentConfig::ReadWeatherAutoFromJson(
    const nlohmann::json &weather_json)
{
  if (!weather_json.is_object())
  {
    return;
  }
  WeatherAuto.AutoChange = weather_json.value("auto_change", WeatherAuto.AutoChange);
  WeatherAuto.DryFraction =
      weather_json.value("dry_fraction", WeatherAuto.DryFraction);
  WeatherAuto.PrecipFraction =
      weather_json.value("precip_fraction", WeatherAuto.PrecipFraction);
  WeatherAuto.DryClearFraction =
      weather_json.value("dry_clear_fraction", WeatherAuto.DryClearFraction);
  WeatherAuto.MinPrecipMinutes =
      weather_json.value("min_precip_minutes", WeatherAuto.MinPrecipMinutes);
  WeatherAuto.MinDryMinutes =
      weather_json.value("min_dry_minutes", WeatherAuto.MinDryMinutes);
  WeatherAuto.TransitionSeconds =
      weather_json.value("transition_seconds", WeatherAuto.TransitionSeconds);
  WeatherAuto.EpisodeMinMinutes =
      weather_json.value("episode_min_minutes", WeatherAuto.EpisodeMinMinutes);
  WeatherAuto.EpisodeMaxMinutes =
      weather_json.value("episode_max_minutes", WeatherAuto.EpisodeMaxMinutes);
  WeatherAuto.PrecipHeavyFraction = weather_json.value(
      "precip_heavy_fraction", WeatherAuto.PrecipHeavyFraction);

  WeatherRuntime.Enabled =
      weather_json.value("auto_enabled", WeatherRuntime.Enabled);
  WeatherRuntime.ManualOverride = weather_json.value(
      "auto_manual_override", WeatherRuntime.ManualOverride);
  WeatherRuntime.EpisodeRemainingSec = weather_json.value(
      "auto_remaining_sec", WeatherRuntime.EpisodeRemainingSec);
  const std::string mode =
      weather_json.value("auto_episode_mode", std::string("none"));
  if (mode == "dry")
  {
    WeatherRuntime.EpisodeMode = WeatherAutoEpisodeMode::Dry;
  }
  else if (mode == "precip")
  {
    WeatherRuntime.EpisodeMode = WeatherAutoEpisodeMode::Precip;
  }
  else
  {
    WeatherRuntime.EpisodeMode = WeatherAutoEpisodeMode::None;
  }
}

void EnvironmentConfig::WriteWeatherAutoToJson(
    nlohmann::json &weather_json) const
{
  weather_json["auto_change"] = WeatherAuto.AutoChange;
  weather_json["dry_fraction"] = WeatherAuto.DryFraction;
  weather_json["precip_fraction"] = WeatherAuto.PrecipFraction;
  weather_json["dry_clear_fraction"] = WeatherAuto.DryClearFraction;
  weather_json["min_precip_minutes"] = WeatherAuto.MinPrecipMinutes;
  weather_json["min_dry_minutes"] = WeatherAuto.MinDryMinutes;
  weather_json["transition_seconds"] = WeatherAuto.TransitionSeconds;
  weather_json["episode_min_minutes"] = WeatherAuto.EpisodeMinMinutes;
  weather_json["episode_max_minutes"] = WeatherAuto.EpisodeMaxMinutes;
  weather_json["precip_heavy_fraction"] = WeatherAuto.PrecipHeavyFraction;
  weather_json["auto_enabled"] = WeatherRuntime.Enabled;
  weather_json["auto_manual_override"] = WeatherRuntime.ManualOverride;
  weather_json["auto_remaining_sec"] = WeatherRuntime.EpisodeRemainingSec;
  switch (WeatherRuntime.EpisodeMode)
  {
  case WeatherAutoEpisodeMode::Dry:
    weather_json["auto_episode_mode"] = "dry";
    break;
  case WeatherAutoEpisodeMode::Precip:
    weather_json["auto_episode_mode"] = "precip";
    break;
  case WeatherAutoEpisodeMode::None:
  default:
    weather_json["auto_episode_mode"] = "none";
    break;
  }
}

EnvironmentConfig EnvironmentConfig::FromJson(const nlohmann::json &root)
{
  EnvironmentConfig config;
  if (!root.is_object())
  {
    return config;
  }
  config.TimeOfDay = root.value("time_of_day", config.TimeOfDay);
  config.DayLengthMinutes =
      root.value("day_length_minutes", config.DayLengthMinutes);
  if (root.contains("lighting") && root["lighting"].is_object())
  {
    config.MinAmbient =
        root["lighting"].value("min_ambient", config.MinAmbient);
  }
  if (root.contains("celestial") && root["celestial"].is_object())
  {
    const nlohmann::json &celestial = root["celestial"];
    config.CelestialHorizonFade =
        celestial.value("horizon_fade", config.CelestialHorizonFade);
    if (celestial.contains("bodies"))
    {
      TryParseCelestialBodiesArray(celestial["bodies"], config.CelestialBodies);
    }
  }
  if (root.contains("celestial_bodies") && root["celestial_bodies"].is_array())
  {
    TryParseCelestialBodiesArray(root["celestial_bodies"],
                                 config.CelestialBodies);
  }
  if (root.contains("weather_auto") && root["weather_auto"].is_object())
  {
    config.ReadWeatherAutoFromJson(root["weather_auto"]);
  }
  else if (root.contains("weather") && root["weather"].is_object())
  {
    config.ReadWeatherAutoFromJson(root["weather"]);
  }
  config.Validate();
  return config;
}

nlohmann::json EnvironmentConfig::ToJson() const
{
  nlohmann::json root;
  root["time_of_day"] = TimeOfDay;
  root["day_length_minutes"] = DayLengthMinutes;
  root["lighting"] = nlohmann::json::object();
  root["lighting"]["min_ambient"] = MinAmbient;
  root["celestial"] = nlohmann::json::object();
  root["celestial"]["horizon_fade"] = CelestialHorizonFade;
  nlohmann::json bodies = nlohmann::json::array();
  for (const EnvironmentCelestialBodySpec &body : CelestialBodies)
  {
    nlohmann::json item;
    item["id"] = body.Id;
    item["type"] = body.Type;
    item["color"] =
        nlohmann::json::array({body.Color.r, body.Color.g, body.Color.b});
    item["intensity"] = body.Intensity;
    item["angular_size_deg"] = body.AngularSizeDeg;
    item["orbit_inclination_deg"] = body.OrbitInclinationDeg;
    item["orbit_period_days"] = body.OrbitPeriodDays;
    item["orbit_phase"] = body.OrbitPhase;
    item["orbit_longitude_deg"] = body.OrbitLongitudeDeg;
    bodies.push_back(item);
  }
  root["celestial"]["bodies"] = bodies;
  nlohmann::json weather = nlohmann::json::object();
  WriteWeatherAutoToJson(weather);
  root["weather"] = weather;
  return root;
}

} // namespace cutum
