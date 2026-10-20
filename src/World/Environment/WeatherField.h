#pragma once

#include "WorldGen/Sampling/BiomeTypes.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

struct BiomeEnvironmentSample
{
  BiomeBlend BiomeWeights;
  int SurfaceHeight{0};
  float Temperature{0.5f};
  float Moisture{0.5f};
  float PrecipitationMultiplier{1.0f};
  float WindExposure{1.0f};
};

struct WeatherSample
{
  float CloudCoverage{0.0f};
  float PrecipitationIntensity{0.0f};
  float SnowFraction{0.0f};
  glm::vec2 WindDirection{1.0f, 0.0f};
  float WindStrength{0.2f};
  float FogFactor{1.0f};
  float SkyLightAttenuation{1.0f};
  float SurfaceWetness{0.0f};
};

/// Deterministic, seed-derived weather fronts. The field is advected through
/// world space and needs only its algorithm version and simulation clock to
/// resume after a save/load.
class WeatherFrontField
{
public:
  static constexpr uint32_t kAlgorithmVersion = 2;

  explicit WeatherFrontField(uint32_t seed = 0) : Seed(seed) {}
  void SetSeed(uint32_t seed) { Seed = seed; }
  uint32_t GetSeed() const { return Seed; }
  void Tick(float dt_seconds);
  double GetSimulationTimeSec() const { return SimulationTimeSec; }
  void SetSimulationTimeSec(double seconds);

  WeatherSample
  SampleAt(const glm::vec3 &position, const BiomeEnvironmentSample &biome,
           float sea_level, float temperature_lapse_per_block,
           float precip_fraction, float precip_heavy_fraction,
           float min_precip_minutes = 2.0f, float min_dry_minutes = 1.0f,
           float episode_min_minutes = 3.0f, float episode_max_minutes = 12.0f,
           float transition_seconds = 45.0f) const;
  float SampleCloudCoverageAt(const glm::vec2 &position_xz) const;

private:
  uint32_t Seed{0};
  double SimulationTimeSec{0.0};
};

} // namespace cutum
