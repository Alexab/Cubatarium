#include "World/Environment/WeatherAutoController.h"

#include "World/Core/World.h"
#include "World/Environment/WeatherBiomeUtil.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace cutum
{

namespace
{

std::mt19937 MakeEpisodeRng(const UWorld &world,
                            const WeatherAutoRuntime &runtime)
{
  std::seed_seq seed{
      static_cast<uint32_t>(world.GetWorldSeed()),
      static_cast<uint32_t>(runtime.EpisodeRemainingSec * 1000.0f),
      static_cast<uint32_t>(world.GetEnvironmentState().TimeOfDayNormalized *
                            100000.0f)};
  return std::mt19937(seed);
}

float RandomUnit(std::mt19937 &rng)
{
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng);
}

float RandomRange(std::mt19937 &rng, float min_value, float max_value)
{
  if (max_value <= min_value)
  {
    return min_value;
  }
  std::uniform_real_distribution<float> dist(min_value, max_value);
  return dist(rng);
}

UWorld::WeatherType PickDryWeather(const WeatherAutoSettings &settings,
                                   std::mt19937 &rng)
{
  return RandomUnit(rng) < settings.DryClearFraction
             ? UWorld::WeatherType::Clear
             : UWorld::WeatherType::Cloudy;
}

UWorld::WeatherType PickPrecipWeather(const UWorld &world,
                                      const WeatherAutoSettings &settings,
                                      std::mt19937 &rng)
{
  BiomeId biome = BiomeId::Plains;
  if (!ResolvePlayerBiome(world, biome))
  {
    biome = BiomeId::Plains;
  }
  const WeatherClimateGroup climate = MapBiomeToClimateGroup(biome);
  if (climate == WeatherClimateGroup::Arid)
  {
    return UWorld::WeatherType::Cloudy;
  }
  const bool heavy = RandomUnit(rng) < settings.PrecipHeavyFraction;
  if (climate == WeatherClimateGroup::Cold)
  {
    return heavy ? UWorld::WeatherType::Storm : UWorld::WeatherType::Snow;
  }
  return heavy ? UWorld::WeatherType::Storm : UWorld::WeatherType::Rain;
}

void BeginEpisode(UWorld &world, WeatherAutoEpisodeMode mode,
                  UWorld::WeatherType weather, float duration_minutes)
{
  EnvironmentConfig &config = world.GetEnvironmentConfigMutable();
  config.WeatherRuntime.EpisodeMode = mode;
  config.WeatherRuntime.EpisodeRemainingSec =
      std::max(1.0f, duration_minutes * 60.0f);
  world.SetWeatherInternal(weather, config.WeatherAuto.TransitionSeconds, false);
}

void StartDryEpisode(UWorld &world, const WeatherAutoSettings &settings,
                     std::mt19937 &rng)
{
  const float min_minutes =
      std::max(settings.MinDryMinutes, settings.EpisodeMinMinutes);
  const float duration = RandomRange(rng, min_minutes, settings.EpisodeMaxMinutes);
  BeginEpisode(world, WeatherAutoEpisodeMode::Dry, PickDryWeather(settings, rng),
               duration);
}

void StartNextEpisode(UWorld &world)
{
  EnvironmentConfig &config = world.GetEnvironmentConfigMutable();
  const WeatherAutoSettings &settings = config.WeatherAuto;
  std::mt19937 rng = MakeEpisodeRng(world, config.WeatherRuntime);

  const bool precip_episode = RandomUnit(rng) >= settings.DryFraction;
  if (!precip_episode)
  {
    StartDryEpisode(world, settings, rng);
    return;
  }

  UWorld::WeatherType weather = PickPrecipWeather(world, settings, rng);
  BiomeId biome = BiomeId::Plains;
  ResolvePlayerBiome(world, biome);
  if (MapBiomeToClimateGroup(biome) == WeatherClimateGroup::Arid)
  {
    StartDryEpisode(world, settings, rng);
    return;
  }

  const float min_minutes =
      std::max(settings.MinPrecipMinutes, settings.EpisodeMinMinutes);
  const float duration = RandomRange(rng, min_minutes, settings.EpisodeMaxMinutes);
  BeginEpisode(world, WeatherAutoEpisodeMode::Precip, weather, duration);
}

} // namespace

void UWeatherAutoController::Tick(UWorld &world, float dt_seconds)
{
  if (dt_seconds <= 0.0f || !std::isfinite(dt_seconds))
  {
    return;
  }
  EnvironmentConfig &config = world.GetEnvironmentConfigMutable();
  if (!config.WeatherAuto.AutoChange || !config.WeatherRuntime.Enabled ||
      config.WeatherRuntime.ManualOverride)
  {
    return;
  }
  if (config.WeatherRuntime.EpisodeRemainingSec <= 0.0f)
  {
    StartNextEpisode(world);
    return;
  }
  config.WeatherRuntime.EpisodeRemainingSec =
      std::max(0.0f, config.WeatherRuntime.EpisodeRemainingSec - dt_seconds);
  if (config.WeatherRuntime.EpisodeRemainingSec <= 0.0f)
  {
    StartNextEpisode(world);
  }
}

} // namespace cutum
