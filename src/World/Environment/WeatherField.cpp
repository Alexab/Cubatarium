#include "World/Environment/WeatherField.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cutum
{
namespace
{

constexpr float kCellSize = 512.0f;
constexpr float kEdgeBlend = 96.0f;

uint32_t Hash(uint32_t seed, int x, int z, uint32_t salt)
{
  uint32_t value = seed ^ salt;
  value ^= static_cast<uint32_t>(x) * 0x9E3779B9u;
  value = (value ^ (value >> 16)) * 0x85EBCA6Bu;
  value ^= static_cast<uint32_t>(z) * 0xC2B2AE35u;
  value = (value ^ (value >> 13)) * 0x27D4EB2Fu;
  value ^= value >> 16;
  return value;
}

float Unit(uint32_t value)
{
  return static_cast<float>(value & 0x00FFFFFFu) / 16777215.0f;
}

float Smoothstep(float edge0, float edge1, float x)
{
  const float t =
      std::clamp((x - edge0) / std::max(1.0e-6f, edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float PrecipitationEpisodeWeight(
    uint32_t seed, int cell_x, int cell_z, double simulation_time_sec,
    float min_precip_minutes, float min_dry_minutes, float episode_min_minutes,
    float episode_max_minutes, float transition_seconds)
{
  const double minimum_duration =
      60.0 * std::max({0.1f, min_precip_minutes, episode_min_minutes});
  const double maximum_duration =
      60.0 * std::max(static_cast<float>(minimum_duration / 60.0),
                      episode_max_minutes);
  const double dry_duration = 60.0 * std::max(0.0f, min_dry_minutes);
  const double transition_buffer = 2.0 * std::max(0.0f, transition_seconds);
  const double cycle_duration =
      std::max(1.0, maximum_duration + dry_duration + transition_buffer);
  const float phase_offset = Unit(Hash(seed, cell_x, cell_z, 0x7F4A7C15u)) *
                             static_cast<float>(cycle_duration);
  const double local_cycle = simulation_time_sec + phase_offset;
  const uint32_t interval =
      static_cast<uint32_t>(std::floor(local_cycle / cycle_duration));
  double phase = std::fmod(local_cycle, cycle_duration);
  if (phase < 0.0)
  {
    phase += cycle_duration;
  }

  const float duration_variation =
      Unit(Hash(seed, cell_x, cell_z, 0x94D049BBu ^ interval));
  const double episode_duration =
      minimum_duration +
      (maximum_duration - minimum_duration) * duration_variation;
  const float transition = static_cast<float>(
      std::clamp(static_cast<double>(transition_seconds), 0.0,
                 std::min(episode_duration * 0.25, cycle_duration * 0.25)));
  if (transition <= 1.0e-3f)
  {
    return phase < episode_duration ? 1.0f : 0.0f;
  }
  const float start = Smoothstep(0.0f, transition, static_cast<float>(phase));
  const float end =
      1.0f - Smoothstep(static_cast<float>(episode_duration - transition),
                        static_cast<float>(episode_duration + transition),
                        static_cast<float>(phase));
  return std::clamp(start * end, 0.0f, 1.0f);
}

} // namespace

void WeatherFrontField::Tick(float dt_seconds)
{
  if (std::isfinite(dt_seconds) && dt_seconds > 0.0f)
  {
    SimulationTimeSec += static_cast<double>(dt_seconds);
  }
}

void WeatherFrontField::SetSimulationTimeSec(double seconds)
{
  SimulationTimeSec = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
}

WeatherSample WeatherFrontField::SampleAt(
    const glm::vec3 &position, const BiomeEnvironmentSample &biome,
    float sea_level, float temperature_lapse_per_block, float precip_fraction,
    float precip_heavy_fraction, float min_precip_minutes,
    float min_dry_minutes, float episode_min_minutes, float episode_max_minutes,
    float transition_seconds) const
{
  WeatherSample result;
  const float base_angle = Unit(Hash(Seed, 0, 0, 0xA511E9B3u)) * 6.2831853f;
  const glm::vec2 prevailing(std::cos(base_angle), std::sin(base_angle));
  const float speed = 0.5f + Unit(Hash(Seed, 0, 0, 0x63D83595u));
  const float time = static_cast<float>(SimulationTimeSec);
  const glm::vec2 advected =
      glm::vec2(position.x, position.z) - prevailing * (speed * time);
  const int cell_x = static_cast<int>(std::floor(advected.x / kCellSize));
  const int cell_z = static_cast<int>(std::floor(advected.y / kCellSize));

  float cloud = 0.06f;
  float precipitation = 0.0f;
  glm::vec2 local_wind(0.0f);
  float wind_weight = 0.0f;
  const float climate_precip =
      std::clamp(biome.PrecipitationMultiplier, 0.0f, 4.0f);
  const float moisture_factor =
      0.6f + std::clamp(biome.Moisture, 0.0f, 1.0f) * 0.8f;
  const float front_precip_chance = std::clamp(
      precip_fraction * climate_precip * moisture_factor, 0.0f, 1.0f);
  const float heavy_fraction = std::clamp(precip_heavy_fraction, 0.0f, 1.0f);

  // Front radii can exceed a cell, so include the full support plus one cell
  // of margin. Smooth radial edges make adjacent fronts blend continuously.
  for (int dz = -2; dz <= 2; ++dz)
  {
    for (int dx = -2; dx <= 2; ++dx)
    {
      const int cx = cell_x + dx;
      const int cz = cell_z + dz;
      const uint32_t cloud_hash = Hash(Seed, cx, cz, 0xC0A57E11u);
      const uint32_t rain_hash = Hash(Seed, cx, cz, 0xB5297A4Du);
      const uint32_t shape_hash = Hash(Seed, cx, cz, 0x68E31DA4u);
      const glm::vec2 center(
          (static_cast<float>(cx) + 0.5f + (Unit(shape_hash) - 0.5f) * 0.5f) *
              kCellSize,
          (static_cast<float>(cz) + 0.5f +
           (Unit(Hash(shape_hash, cx, cz, 0x1B56C4E9u)) - 0.5f) * 0.5f) *
              kCellSize);
      const glm::vec2 delta = advected - center;
      const float distance = glm::length(delta);
      const float radius =
          384.0f + Unit(Hash(shape_hash, cx, cz, 0x9E3779B9u)) * 384.0f;
      const float edge =
          1.0f - Smoothstep(radius - kEdgeBlend, radius + kEdgeBlend, distance);
      if (edge <= 0.001f)
      {
        continue;
      }

      if (Unit(cloud_hash) < 0.34f)
      {
        const float cloud_amount =
            edge *
            (0.45f + Unit(Hash(cloud_hash, cx, cz, 0x85EBCA6Bu)) * 0.55f);
        cloud = std::max(cloud, cloud_amount);
      }

      // Every potential precipitation front carries a dense cloud body. The
      // climate and episode masks below decide whether that body actually rains.
      if (Unit(rain_hash) < 0.22f)
      {
        const float storm_cloud =
            edge * (0.62f + Unit(Hash(rain_hash, cx, cz, 0x3C6EF372u)) * 0.38f);
        cloud = std::max(cloud, storm_cloud);
      }

      if (Unit(rain_hash) < front_precip_chance * 0.22f)
      {
        const float episode_weight = PrecipitationEpisodeWeight(
            Seed, cx, cz, SimulationTimeSec, min_precip_minutes,
            min_dry_minutes, episode_min_minutes, episode_max_minutes,
            transition_seconds);
        if (episode_weight <= 0.001f)
        {
          continue;
        }
        const float severity = Unit(Hash(rain_hash, cx, cz, 0xC2B2AE35u));
        const float front_intensity = severity > 1.0f - heavy_fraction
                                          ? 0.72f + severity * 0.28f
                                          : 0.38f + severity * 0.42f;
        precipitation =
            std::max(precipitation, edge * front_intensity * episode_weight);
        // Fronts advect with the prevailing wind. A radial vector from each
        // front's centre made the field produce divergent, implausible winds.
        local_wind += prevailing * (edge * episode_weight);
        wind_weight += edge * episode_weight;
      }
    }
  }

  const float altitude = static_cast<float>(biome.SurfaceHeight) - sea_level;
  const float temperature = std::clamp(
      biome.Temperature - std::max(0.0f, altitude) *
                              std::max(0.0f, temperature_lapse_per_block),
      0.0f, 1.0f);
  result.CloudCoverage = std::clamp(cloud, 0.0f, 1.0f);
  result.PrecipitationIntensity = std::clamp(precipitation, 0.0f, 1.0f);
  // 0.24 and 0.36 form a continuous rain/snow transition band.
  result.SnowFraction = 1.0f - Smoothstep(0.24f, 0.36f, temperature);
  result.WindDirection =
      wind_weight > 1.0e-4f && glm::length(local_wind) > 1.0e-4f
          ? glm::normalize(local_wind)
          : prevailing;
  result.WindStrength =
      std::clamp((0.2f + result.PrecipitationIntensity * 0.75f) *
                     std::clamp(biome.WindExposure, 0.0f, 2.0f),
                 0.0f, 1.0f);
  result.FogFactor = std::clamp(1.0f + result.CloudCoverage * 0.12f +
                                    result.PrecipitationIntensity * 0.5f,
                                1.0f, 1.8f);
  result.SkyLightAttenuation =
      std::clamp(1.0f - result.CloudCoverage * 0.28f -
                     result.PrecipitationIntensity * 0.12f,
                 0.55f, 1.0f);
  result.SurfaceWetness = result.PrecipitationIntensity *
                          (1.0f - result.SnowFraction * 0.65f) * 0.85f;
  return result;
}

float WeatherFrontField::SampleCloudCoverageAt(
    const glm::vec2 &position_xz) const
{
  const float base_angle = Unit(Hash(Seed, 0, 0, 0xA511E9B3u)) * 6.2831853f;
  const glm::vec2 prevailing(std::cos(base_angle), std::sin(base_angle));
  const float speed = 0.5f + Unit(Hash(Seed, 0, 0, 0x63D83595u));
  const glm::vec2 advected =
      position_xz -
      prevailing * (speed * static_cast<float>(SimulationTimeSec));
  const int cell_x = static_cast<int>(std::floor(advected.x / kCellSize));
  const int cell_z = static_cast<int>(std::floor(advected.y / kCellSize));
  float cloud = 0.06f;
  for (int dz = -2; dz <= 2; ++dz)
  {
    for (int dx = -2; dx <= 2; ++dx)
    {
      const int cx = cell_x + dx;
      const int cz = cell_z + dz;
      const uint32_t cloud_hash = Hash(Seed, cx, cz, 0xC0A57E11u);
      const uint32_t rain_hash = Hash(Seed, cx, cz, 0xB5297A4Du);
      if (Unit(cloud_hash) >= 0.34f)
      {
        if (Unit(rain_hash) >= 0.22f)
        {
          continue;
        }
      }
      const uint32_t shape_hash = Hash(Seed, cx, cz, 0x68E31DA4u);
      const glm::vec2 center(
          (static_cast<float>(cx) + 0.5f + (Unit(shape_hash) - 0.5f) * 0.5f) *
              kCellSize,
          (static_cast<float>(cz) + 0.5f +
           (Unit(Hash(shape_hash, cx, cz, 0x1B56C4E9u)) - 0.5f) * 0.5f) *
              kCellSize);
      const float radius =
          384.0f + Unit(Hash(shape_hash, cx, cz, 0x9E3779B9u)) * 384.0f;
      const float edge =
          1.0f - Smoothstep(radius - kEdgeBlend, radius + kEdgeBlend,
                            glm::length(advected - center));
      float amount = 0.0f;
      if (Unit(cloud_hash) < 0.34f)
      {
        amount = edge *
                 (0.45f +
                  Unit(Hash(cloud_hash, cx, cz, 0x85EBCA6Bu)) * 0.55f);
      }
      if (Unit(rain_hash) < 0.22f)
      {
        amount = std::max(
            amount,
            edge * (0.62f + Unit(Hash(rain_hash, cx, cz, 0x3C6EF372u)) * 0.38f));
      }
      cloud = std::max(cloud, amount);
    }
  }
  return std::clamp(cloud, 0.0f, 1.0f);
}

} // namespace cutum
