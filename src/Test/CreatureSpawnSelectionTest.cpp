#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Spawning/CreatureSpawnSelection.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
struct Species
{
  std::string id;
  cutum::CreatureHabitat habitat;
};

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_spawn_selection_test: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;
  const std::vector<WeightedSpawnOption<Species>> species = {
      {{"bad_weight", CreatureHabitat::Terrestrial}, 0.f},
      {{"wolf", CreatureHabitat::Terrestrial}, 1.f},
      {{"trout", CreatureHabitat::Aquatic}, 3.f},
      {{"crab", CreatureHabitat::Amphibious}, 2.f},
      {{"owl", CreatureHabitat::Aerial}, 4.f},
      {{"lava_flan", CreatureHabitat::Lava}, 1.f}};

  EnvironmentSample ground;
  ground.onSolidGround = true;
  auto groundOptions = FilterViableSpawnOptions(
      species, [&](const Species &candidate)
      { return HabitatMatches(candidate.habitat, ground); });
  Expect(groundOptions.size() == 2,
         "a ground point keeps only ground-compatible species", failures);
  Expect(TotalSpawnOptionWeight(groundOptions) == 3.f,
         "only viable species contribute their selection weights", failures);
  const auto *groundFirst = ChooseWeightedSpawnOption(groundOptions, 0.f);
  const auto *groundSecond = ChooseWeightedSpawnOption(groundOptions, 1.f);
  const auto *groundAtUpperBound = ChooseWeightedSpawnOption(groundOptions, 3.f);
  Expect(groundFirst && groundFirst->value.id == "wolf",
         "weighted selection starts with the first viable species", failures);
  Expect(groundSecond && groundSecond->value.id == "crab",
         "selection advances at a species weight boundary", failures);
  Expect(groundAtUpperBound && groundAtUpperBound->value.id == "crab",
         "selection handles an inclusive random upper endpoint", failures);

  EnvironmentSample water;
  water.inFluid = true;
  water.inWater = true;
  auto waterOptions = FilterViableSpawnOptions(
      species, [&](const Species &candidate)
      { return HabitatMatches(candidate.habitat, water); });
  Expect(waterOptions.size() == 2,
         "a water point keeps aquatic and amphibious species", failures);
  Expect(TotalSpawnOptionWeight(waterOptions) == 5.f,
         "water selection excludes incompatible and zero-weight species",
         failures);

  EnvironmentSample openAirOverWater;
  openAirOverWater.inOpenAir = true;
  openAirOverWater.inFluid = true;
  openAirOverWater.inWater = true;
  auto aerialOptions = FilterViableSpawnOptions(
      species, [&](const Species &candidate)
      { return HabitatMatches(candidate.habitat, openAirOverWater); });
  Expect(aerialOptions.size() == 2,
         "aerial species remain excluded from liquid spawn points", failures);
  Expect(aerialOptions[0].value.id == "trout" &&
             aerialOptions[1].value.id == "crab",
         "open air over water selects only liquid-compatible species", failures);

  EnvironmentSample lava;
  lava.inFluid = true;
  lava.inLava = true;
  auto lavaOptions = FilterViableSpawnOptions(
      species, [&](const Species &candidate)
      { return HabitatMatches(candidate.habitat, lava); });
  Expect(lavaOptions.size() == 1,
         "a lava point excludes species from other habitats", failures);
  Expect(lavaOptions[0].value.id == "lava_flan",
         "lava habitat selects the natural lava species", failures);

  const std::vector<WeightedSpawnOption<Species>> none;
  Expect(ChooseWeightedSpawnOption(none, 0.f) == nullptr,
         "empty candidate set produces no selection", failures);

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_spawn_selection_test: OK\n";
  return 0;
}
