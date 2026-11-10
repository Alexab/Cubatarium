#include "World/Environment/WeatherField.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "weather_front_field_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::BiomeEnvironmentSample biome;
  biome.SurfaceHeight = 72;
  biome.Temperature = 0.5f;
  biome.Moisture = 0.6f;
  biome.PrecipitationMultiplier = 1.0f;
  biome.WindExposure = 1.0f;

  cutum::WeatherFrontField field(0xC0B47AULL);
  const glm::vec3 sample_position(-1300.0f, 80.0f, 527.0f);
  const auto first =
      field.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  const auto repeat =
      field.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(first.CloudCoverage == repeat.CloudCoverage &&
             first.PrecipitationIntensity == repeat.PrecipitationIntensity,
         "sample is deterministic at fixed seed and field time");
  Expect(first.CloudCoverage >= 0.0f && first.CloudCoverage <= 1.0f &&
             first.PrecipitationIntensity >= 0.0f &&
             first.PrecipitationIntensity <= 1.0f &&
             first.SnowFraction >= 0.0f && first.SnowFraction <= 1.0f &&
             first.FogFactor >= 1.0f && first.FogFactor <= 1.8f &&
             first.SkyLightAttenuation >= 0.55f &&
             first.SkyLightAttenuation <= 1.0f &&
             first.SurfaceWetness >= 0.0f && first.SurfaceWetness <= 1.0f &&
             first.WindStrength >= 0.0f && first.WindStrength <= 1.0f,
         "weather outputs stay in normalized bounds");
  Expect(std::isfinite(first.WindDirection.x) &&
             std::isfinite(first.WindDirection.y),
         "wind direction stays finite");

  cutum::BiomeEnvironmentSample arid_biome = biome;
  arid_biome.PrecipitationMultiplier = 0.0f;
  const auto arid =
      field.SampleAt(sample_position, arid_biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(arid.PrecipitationIntensity == 0.0f,
         "arid biome profile suppresses precipitation");

  const auto nearby = field.SampleAt(sample_position + glm::vec3(1, 0, 0),
                                     biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(std::abs(first.CloudCoverage - nearby.CloudCoverage) < 0.08f &&
             std::abs(first.PrecipitationIntensity -
                      nearby.PrecipitationIntensity) < 0.08f,
         "front edges blend smoothly at block scale");

  field.Tick(600.0f);
  const auto moved =
      field.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  bool field_moved = false;
  cutum::WeatherFrontField initial_field(0xC0B47AULL);
  for (int z = -2048; z <= 2048 && !field_moved; z += 128)
  {
    for (int x = -2048; x <= 2048; x += 128)
    {
      const glm::vec2 point(static_cast<float>(x), static_cast<float>(z));
      if (std::abs(initial_field.SampleCloudCoverageAt(point) -
                   field.SampleCloudCoverageAt(point)) > 1.0e-3f)
      {
        field_moved = true;
        break;
      }
    }
  }
  Expect(field_moved, "weather field moves with simulation time");

  cutum::WeatherFrontField restored(0xC0B47AULL);
  restored.SetSimulationTimeSec(field.GetSimulationTimeSec());
  const auto replayed =
      restored.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(replayed.CloudCoverage == moved.CloudCoverage &&
             replayed.PrecipitationIntensity == moved.PrecipitationIntensity,
         "seed and saved field clock reproduce the same weather");

  biome.Temperature = 0.2f;
  const auto cold =
      restored.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  biome.Temperature = 0.8f;
  const auto warm =
      restored.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(cold.SnowFraction > warm.SnowFraction,
         "temperature smoothly controls rain and snow phase");
  biome.SurfaceHeight = 48;
  biome.Temperature = 0.3f;
  const auto transition =
      restored.SampleAt(sample_position, biome, 48.0f, 0.00135f, 0.3f, 0.35f);
  Expect(transition.SnowFraction > 0.45f &&
             transition.SnowFraction < 0.55f,
         "temperature threshold blends rain and snow continuously");
  return 0;
}
