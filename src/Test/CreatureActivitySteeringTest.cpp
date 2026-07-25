#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Activity/IUWorldPerception.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>
#include <optional>
#include <unordered_set>
#include <vector>

namespace
{

class UFeelerPerceptionMock : public cutum::IUWorldPerception
{
public:
  std::unordered_set<long long> BlockedCells;

  static long long Key(int x, int z)
  {
    return (static_cast<long long>(x) << 32) ^
           static_cast<unsigned int>(z);
  }

  void BlockCell(int x, int z) { BlockedCells.insert(Key(x, z)); }

  bool CellClear(const glm::vec3 &origin) const
  {
    const int x = static_cast<int>(std::floor(origin.x + 0.5f));
    const int z = static_cast<int>(std::floor(origin.z + 0.5f));
    return BlockedCells.count(Key(x, z)) == 0;
  }

  std::optional<cutum::ControlledCreatureInfo>
  QueryControlledCreatureInfo() const override
  {
    return std::nullopt;
  }
  std::vector<cutum::CreatureId>
  CreaturesInRadius(const glm::vec3 &, float) const override
  {
    return {};
  }
  std::vector<cutum::CreatureNeighborView>
  QueryCreatureNeighborsInRadius(const glm::vec3 &, float,
                                 cutum::CreatureId) const override
  {
    return {};
  }
  bool CreaturesClearAt(const glm::vec3 &body_origin, const glm::vec3 &,
                        cutum::CreatureId) const override
  {
    return CellClear(body_origin);
  }
  std::optional<glm::vec3>
  GetCreatureBodyOrigin(cutum::CreatureId) const override
  {
    return std::nullopt;
  }
  bool CanCreatureOccupyAt(cutum::CreatureHabitat, const glm::vec3 &body,
                           const glm::vec3 &) const override
  {
    return CellClear(body);
  }
  bool HabitatAllowsAt(cutum::CreatureHabitat, const glm::vec3 &body,
                       const glm::vec3 &) const override
  {
    return CellClear(body);
  }
  bool HabitatAllowsMovementAt(cutum::CreatureHabitat, const glm::vec3 &body,
                               const glm::vec3 &) const override
  {
    return CellClear(body);
  }
  bool IsWithinActivityRange(const glm::vec3 &) const override { return true; }
};

} // namespace

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_activity_steering_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const glm::vec3 prev(0.0f, 64.0f, 0.0f);
  const glm::vec3 stuck(0.0f, 64.0f, 0.0f);
  const glm::vec3 moved(0.2f, 64.0f, 0.0f);
  Expect(cutum::IsLocomotionStuck(prev, stuck, 0.1f, 0.05f),
         "zero movement should be stuck");
  Expect(!cutum::IsLocomotionStuck(prev, moved, 0.1f, 0.05f),
         "fast movement should not be stuck");
  const glm::vec3 vertical(0.0f, 64.2f, 0.0f);
  Expect(!cutum::IsLocomotionStuck(prev, vertical, 0.1f, 0.05f),
         "vertical travel should not count as stuck");

  const glm::vec3 base(1.0f, 0.0f, 0.0f);
  const glm::vec3 sep(0.0f, 0.0f, 1.0f);
  const glm::vec3 blended =
      cutum::BlendLocomotionDirection(base, sep, 0.5f);
  Expect(std::abs(blended.x) > 0.1f && std::abs(blended.z) > 0.1f,
         "blend should mix base and separation");

  std::vector<cutum::CreatureNeighborView> neighbors;
  cutum::CreatureNeighborView near_neighbor;
  near_neighbor.Id = 2;
  near_neighbor.bodyOrigin = glm::vec3(0.5f, 0.0f, 0.0f);
  neighbors.push_back(near_neighbor);
  const glm::vec3 push = cutum::ComputeSeparationDirection(
      glm::vec3(0.0f), glm::vec3(0.6f, 1.8f, 0.6f), neighbors, 1.0f);
  Expect(push.x < -0.1f, "separation should push away from neighbor");

  const float radius = cutum::SeparationQueryRadius(glm::vec3(0.6f, 1.8f, 0.6f));
  Expect(radius > 1.0f, "separation radius should exceed body footprint");

  Expect(std::abs(cutum::NavigationBodyHeightForBounds(1.85f) - 1.85f) < 1e-4f,
         "nav clearance matches motor body height");
  Expect(std::abs(cutum::NavigationBodyHeightForBounds(0.5f) - 0.5f) < 1e-4f,
         "short mobs keep full nav height");

  // Wall ahead (+X): feelers should bend off the blocked forward cell.
  UFeelerPerceptionMock perception;
  perception.BlockCell(1, 0);
  const glm::vec3 bent = cutum::ApplyWallFeelers(
      perception, cutum::CreatureHabitat::Terrestrial, glm::vec3(0.0f, 64.5f, 0.0f),
      glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.6f, 1.8f, 0.6f), 1, 0.85f);
  Expect(glm::length(bent) > 0.1f, "feelers should return a direction");
  Expect(std::abs(bent.z) > 0.2f || bent.x < 0.9f,
         "wall ahead should bend feeler away from +X");

  std::cout << "creature_activity_steering_test: OK" << std::endl;
  return 0;
}
