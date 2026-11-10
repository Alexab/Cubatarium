#include "Creatures/Environment/CreatureSpawnPlacement.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <string>

namespace
{

int gFailures = 0;

void Require(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL " << message << '\n';
    ++gFailures;
  }
}

void RequireNear(std::optional<float> value, float expected,
                 const std::string &message)
{
  const bool ok = value.has_value() &&
                  std::abs(*value - expected) < 1e-4f;
  if (!ok)
  {
    std::cerr << "FAIL " << message << " (expected " << expected
              << ", got " << (value ? std::to_string(*value) : "nullopt")
              << ")\n";
    ++gFailures;
  }
}

} // namespace

int main()
{
  using cutum::ComputeAquaticSpawnFeetY;

  // Deep column: the spot is mid-way between the floor and the highest
  // fully-submerged position, not glued to the bottom.
  // band: minY=60, maxFeet = 80 - 2 + 0.5 = 78.5 -> mid = 69.25.
  RequireNear(ComputeAquaticSpawnFeetY(60, 80, 2.0f), 69.25f,
              "deep column spawns mid-water");

  // Shallow column where the body barely fits: only half a block of the
  // submerged band is available -> a quarter above the floor.
  // maxFeet = 62 - 2 + 0.5 = 60.5 -> mid = 60.25.
  RequireNear(ComputeAquaticSpawnFeetY(60, 62, 2.0f), 60.25f,
              "half-block band spawns just above the floor");

  // Exactly submerged: maxFeet == minFeet -> floor.
  RequireNear(ComputeAquaticSpawnFeetY(60, 61, 1.5f), 60.0f,
              "exact-fit band spawns at the floor");

  // Too shallow for the body (depth 1 < 4 * 0.75): rejected — giants must not
  // strand in ankle-deep water.
  Require(!ComputeAquaticSpawnFeetY(60, 60, 4.0f).has_value(),
          "column too shallow for the body is rejected");
  Require(!ComputeAquaticSpawnFeetY(60, 61, 4.0f).has_value(),
          "column under 75% body height is rejected");

  // Deep enough by the 75% rule.
  Require(ComputeAquaticSpawnFeetY(60, 63, 4.0f).has_value(),
          "column at 75% body height is accepted");

  // Giant body (whale ~3.3 high) in a deep ocean column: still mid-water.
  // maxFeet = 70 - 3.3 + 0.5 = 67.2 -> mid = 63.6.
  RequireNear(ComputeAquaticSpawnFeetY(60, 70, 3.3f), 63.6f,
              "giant spawns mid-water in deep column");

  if (gFailures != 0)
  {
    std::cerr << gFailures << " aquatic spawn failure(s)\n";
    return 1;
  }
  std::cout << "aquatic spawn feet OK\n";
  return 0;
}
