#include "World/Environment/WeatherBiomeUtil.h"

#include "Creatures/Core/Creature.h"
#include "World/Core/World.h"
#include "WorldGen/Sampling/BiomeSampler.h"

#include <cmath>
#include <optional>

namespace cutum
{

WeatherClimateGroup MapBiomeToClimateGroup(BiomeId biome)
{
  switch (biome)
  {
  case BiomeId::Tundra:
  case BiomeId::ColdSteppe:
    return WeatherClimateGroup::Cold;
  case BiomeId::Desert:
  case BiomeId::Savanna:
    return WeatherClimateGroup::Arid;
  case BiomeId::Plains:
  case BiomeId::Forest:
  case BiomeId::Hills:
  case BiomeId::Foothills:
  case BiomeId::Scrubland:
  default:
    return WeatherClimateGroup::Temperate;
  }
}

bool ResolvePlayerBiome(const UWorld &world, BiomeId &out_biome)
{
  const UCreature *controlled = world.GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const glm::vec3 feet = controlled->GetBodyOrigin();
  const int world_x = static_cast<int>(std::floor(feet.x));
  const int world_z = static_cast<int>(std::floor(feet.z));
  const ProceduralSettings &settings = world.GetProceduralSettings();
  float temperature = 0.0f;
  float moisture = 0.0f;
  ComputeBiomeClimate(world_x, world_z, settings.Seed, temperature, moisture);
  float local_height_norm = 0.5f;
  if (const std::optional<int> surface_y =
          world.FindHighestSolidY(world_x, world_z))
  {
    local_height_norm =
        std::clamp(static_cast<float>(*surface_y - settings.SeaLevel) /
                       static_cast<float>(std::max(
                           1, settings.MaxHeight - settings.SeaLevel)),
                   0.0f, 1.0f);
  }
  out_biome = ClassifyBiome(temperature, moisture, local_height_norm);
  return true;
}

} // namespace cutum
