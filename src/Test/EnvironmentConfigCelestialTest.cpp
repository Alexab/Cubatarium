#include "World/Environment/EnvironmentConfig.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

static void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "environment_config_celestial_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const auto explicit_type = cutum::ParseCelestialBodySpec(
      nlohmann::json{{"id", "custom-object-17"}, {"type", "moon"}});
  Expect(explicit_type.Type == "moon",
         "explicit type works with arbitrary identifier");

  const auto legacy_moon =
      cutum::ParseCelestialBodySpec(nlohmann::json{{"id", "moon_legacy"}});
  Expect(legacy_moon.Type == "moon", "legacy moon identifier fallback");

  const auto legacy_sun =
      cutum::ParseCelestialBodySpec(nlohmann::json{{"id", "primary-star"}});
  Expect(legacy_sun.Type == "sun", "legacy sun fallback");

  const auto legacy_weather = cutum::EnvironmentConfig::FromJson(
      nlohmann::json{{"weather", {{"auto_enabled", true}}}});
  Expect(legacy_weather.WeatherRuntime.FieldSimulationTimeSec == 0.0 &&
             legacy_weather.WeatherRuntime.WeatherSimulationTimeSec == 0.0,
         "old saves default both weather clocks to zero");

  auto weather_config = cutum::EnvironmentConfig::FromJson(
      nlohmann::json{{"weather",
                      {{"auto_enabled", true},
                       {"field_algorithm_version", 1},
                       {"field_simulation_time_sec", 245.5},
                       {"weather_simulation_time_sec", 912.75}}}});
  const auto saved_weather = weather_config.ToJson();
  const auto replay_config = cutum::EnvironmentConfig::FromJson(saved_weather);
  Expect(replay_config.WeatherRuntime.FieldSimulationTimeSec == 245.5 &&
             replay_config.WeatherRuntime.WeatherSimulationTimeSec == 912.75,
         "weather clocks survive environment save and load");
  return 0;
}
