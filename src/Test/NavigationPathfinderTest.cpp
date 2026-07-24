#include "Navigation/UNavigationPathfinder.h"
#include "Navigation/UWaypointFollower.h"
#include "Navigation/WorldNavigationQueries.h"

#include <glm/glm.hpp>

#include <cstdlib>
#include <iostream>
#include <unordered_set>

namespace
{

class UFlatNavigationMock : public cutum::IUWorldNavigation
{
public:
  explicit UFlatNavigationMock(int ground_y) : GroundY(ground_y) {}

  bool IsTerrestrialStandNode(const cutum::NavigationStandNode &node,
                              float /*body_height*/) const override
  {
    return node.ground_y == GroundY;
  }

  bool CanStepTerrestrial(const cutum::NavigationStandNode &from,
                          const cutum::NavigationStandNode &to,
                          float max_jump, float max_drop,
                          float /*body_height*/) const override
  {
    const int dx = std::abs(to.x - from.x);
    const int dz = std::abs(to.z - from.z);
    if (dx + dz != 1)
    {
      return false;
    }
    if (to.ground_y != GroundY)
    {
      return false;
    }
    const float dy = static_cast<float>(to.ground_y - from.ground_y);
    return dy <= max_jump + 0.01f && dy >= -max_drop - 0.01f;
  }

private:
  int GroundY;
};

} // namespace

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "navigation_pathfinder_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  UFlatNavigationMock navigation(64);
  cutum::NavigationQuery query;
  query.search_distance = 8;
  query.body_height = 1.8f;

  const cutum::NavigationPath path = cutum::UNavigationPathfinder::FindTerrestrialPath(
      navigation, glm::vec3(0.0f, 64.5f, 0.0f), glm::vec3(3.0f, 64.5f, 0.0f),
      query);
  Expect(path.valid, "path on flat plane should succeed");
  Expect(path.waypoints.size() >= 2, "path should contain start and goal");

  int waypoint_index = 0;
  const cutum::WaypointFollowResult follow = cutum::UWaypointFollower::Update(
      glm::vec3(0.0f, 64.5f, 0.0f), path, 0.45f, waypoint_index);
  Expect(glm::length(follow.move_dir) > 0.1f, "follower should emit direction");

  std::cout << "navigation_pathfinder_test: OK" << std::endl;
  return 0;
}
