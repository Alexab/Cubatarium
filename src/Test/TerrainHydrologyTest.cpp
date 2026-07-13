#include "WorldGen/Sampling/TerrainHydrology.h"

#include "WorldGen/Core/ProceduralSettings.h"

#include <iostream>
#include <optional>
#include <utility>

namespace
{

constexpr const char *kTestName = "terrain_hydrology_test";

void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << kTestName << ": FAIL " << message << std::endl;
    std::exit(1);
  }
}

std::optional<std::pair<int, int>> FindHydrologyCell(uint32_t seed,
                                                     cutum::TerrainHydrologyClass cls,
                                                     int radius = 256)
{
  for (int x = -radius; x <= radius; ++x)
  {
    for (int z = -radius; z <= radius; ++z)
    {
      if (cutum::ClassifyTerrainHydrology(x, z, seed) == cls)
      {
        return std::make_pair(x, z);
      }
    }
  }
  return std::nullopt;
}

} // namespace

int main()
{
  cutum::ProceduralSettings settings;
  settings.SeaLevel = 48;
  settings.Seed = 424242u;

  const int sea = settings.SeaLevel;

  const auto ocean_cell =
      FindHydrologyCell(settings.Seed, cutum::TerrainHydrologyClass::Ocean);
  Expect(ocean_cell.has_value(), "found ocean macro cell");
  Expect(cutum::FluidFillTopY(ocean_cell->first, ocean_cell->second, sea - 5,
                              settings.Seed, settings) == sea,
         "deep ocean seabed fills to sea");

  const auto land_cell =
      FindHydrologyCell(settings.Seed, cutum::TerrainHydrologyClass::Land);
  Expect(land_cell.has_value(), "found land macro cell");
  const int land_y = cutum::ApplyLandSeaHeightPolicy(
      land_cell->first, land_cell->second, sea - 6, settings.Seed, settings);
  Expect(land_y >= sea + 1, "land macro keeps dry surface above sea");

  const int ocean_y = cutum::ApplyLandSeaHeightPolicy(
      ocean_cell->first, ocean_cell->second, sea + 2, settings.Seed, settings);
  Expect(ocean_y <= sea - 1, "ocean macro keeps floor below sea");

  const auto coast_cell =
      FindHydrologyCell(settings.Seed, cutum::TerrainHydrologyClass::Coast);
  Expect(coast_cell.has_value(), "found coast macro cell");
  const int coast_fill = cutum::FluidFillTopY(coast_cell->first, coast_cell->second,
                                              sea - 1, settings.Seed, settings);
  Expect(coast_fill == sea - 1, "coast shallow fill stops below sea plane");

  const int ocean_fill = cutum::FluidFillTopY(
      ocean_cell->first, ocean_cell->second, sea - 3, settings.Seed, settings);
  Expect(ocean_fill == sea, "ocean column fills to sea level");

  const int land_fill = cutum::FluidFillTopY(land_cell->first, land_cell->second,
                                             sea - 10, settings.Seed, settings);
  Expect(land_fill < sea, "inland depression does not open full ocean column");
  Expect(land_fill > sea - 10, "inland depression still receives local water");

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
