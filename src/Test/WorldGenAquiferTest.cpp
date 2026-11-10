#include "WorldGen/Sampling/AquiferSampler.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{

constexpr const char *kTestName = "worldgen_aquifer_test";

void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << kTestName << ": FAILED: " << message << '\n';
    std::exit(1);
  }
}

void TestAquiferRegionsAndDeterminism()
{
  cutum::AquiferParams params;
  params.DryCellChance = 0.45f;
  params.BarrierBand = 8.0f;
  params.BarrierNoiseThreshold = -1.0f;
  params.PressureDifferenceThreshold = 0;
  params.LavaCellChance = 0.5f;
  constexpr uint32_t seed = 91427;
  constexpr int sea_level = 63;
  cutum::UAquiferSampler sampler(seed, sea_level, 256, params);
  cutum::UAquiferSampler same_seed(seed, sea_level, 256, params);
  cutum::UAquiferSampler other_seed(seed + 1, sea_level, 256, params);

  std::array<int, 4> counts{};
  for (int y = 1; y < 72; y += 2)
  {
    for (int x = -48; x <= 48; x += 2)
    {
      for (int z = -48; z <= 48; z += 2)
      {
        const cutum::AquiferSample sample = sampler.SampleAt(x, y, z);
        ++counts[static_cast<size_t>(sample.Fluid)];
        const cutum::AquiferSample repeated = same_seed.SampleAt(x, y, z);
        Expect(sample.Fluid == repeated.Fluid &&
                   sample.FluidLevel == repeated.FluidLevel,
               "same seed and coordinates produce identical samples");
        if (sample.Fluid == cutum::AquiferFluid::Water)
        {
          Expect(y <= sample.FluidLevel && y < sea_level,
                 "water is below its local fluid level and sea level");
        }
      }
    }
  }

  Expect(counts[static_cast<size_t>(cutum::AquiferFluid::Dry)] > 0,
         "aquifers contain dry regions");
  Expect(counts[static_cast<size_t>(cutum::AquiferFluid::Water)] > 0,
         "aquifers contain water regions");
  Expect(counts[static_cast<size_t>(cutum::AquiferFluid::Lava)] > 0,
         "deep aquifers contain lava regions");
  Expect(counts[static_cast<size_t>(cutum::AquiferFluid::Barrier)] > 0,
         "aquifers contain barriers between incompatible regions");

  for (const uint32_t seed_sample : {1u, 42u, 12345u, 20240625u})
  {
    const cutum::UAquiferSampler seed_sampler(seed_sample, sea_level, 256,
                                               params);
    bool saw_dry = false;
    bool saw_water = false;
    for (int y = 5; y < 64 && !(saw_dry && saw_water); y += 4)
    {
      for (int x = -32; x <= 32 && !(saw_dry && saw_water); x += 5)
      {
        for (int z = -32; z <= 32; z += 5)
        {
          const cutum::AquiferFluid fluid = seed_sampler.SampleAt(x, y, z).Fluid;
          saw_dry = saw_dry || fluid == cutum::AquiferFluid::Dry;
          saw_water = saw_water || fluid == cutum::AquiferFluid::Water;
        }
      }
    }
    Expect(saw_dry && saw_water,
           "different world seeds retain dry and water aquifer regions");
  }

  bool changed_by_seed = false;
  for (int y = 1; y <= 64 && !changed_by_seed; y += 3)
  {
    for (int x = -32; x <= 32 && !changed_by_seed; x += 3)
    {
      for (int z = -32; z <= 32; z += 3)
      {
        const auto first = sampler.SampleAt(x, y, z);
        const auto second = other_seed.SampleAt(x, y, z);
        if (first.Fluid != second.Fluid || first.FluidLevel != second.FluidLevel)
        {
          changed_by_seed = true;
          break;
        }
      }
    }
  }
  Expect(changed_by_seed, "aquifer distribution depends on the world seed");
}

void TestNegativeWorldRange()
{
  cutum::AquiferParams params;
  params.DryCellChance = 0.0f;
  params.LavaCellChance = 0.0f;
  params.BarrierBand = 0.0f;
  const cutum::UAquiferSampler sampler(91427, 63, 319, params, -64);
  const cutum::AquiferSample inside = sampler.SampleAt(12, -32, -7);
  const cutum::AquiferSample bottom = sampler.SampleAt(12, -64, -7);
  const cutum::AquiferSample below = sampler.SampleAt(12, -65, -7);
  Expect(inside.Fluid == cutum::AquiferFluid::Water,
         "aquifer fluid sampling works at negative world heights");
  Expect(bottom.Fluid == cutum::AquiferFluid::Water,
         "aquifer includes the lowest supported block");
  Expect(below.Fluid == cutum::AquiferFluid::Dry,
         "aquifer rejects samples below the world minimum");
}

} // namespace

int main()
{
  TestAquiferRegionsAndDeterminism();
  TestNegativeWorldRange();
  std::cout << kTestName << ": OK\n";
  return 0;
}
