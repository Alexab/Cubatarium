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
  query.max_jump = 1.0f;

  const cutum::NavigationPath path = cutum::UNavigationPathfinder::FindTerrestrialPath(
      navigation, glm::vec3(0.0f, 64.5f, 0.0f), glm::vec3(3.0f, 64.5f, 0.0f),
      query);
  Expect(path.valid, "path on flat plane should succeed");
  Expect(path.waypoints.size() >= 2, "path should contain start and goal");

  // Eye-height goal (~1.62 above feet) must not be treated as a stand node.
  const glm::vec3 feet_body(0.0f, 64.5f, 0.0f);
  const glm::vec3 eye_goal(3.0f, 64.5f + 1.62f, 0.0f);
  const cutum::NavigationPath eye_path =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          navigation, feet_body, eye_goal, query);
  Expect(!eye_path.valid,
         "path to eye-height goal should fail on feet-anchored stand nodes");
  Expect(eye_path.failReason == "goal_invalid",
         "eye-height goal should report goal_invalid");

  const glm::vec3 body_goal(3.0f, 64.5f, 0.0f);
  const cutum::NavigationPath body_path =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          navigation, feet_body, body_goal, query);
  Expect(body_path.valid, "path to body/feet goal should succeed");

  // Start slightly above stand Y within jumpHeight must snap (zombie float).
  const cutum::NavigationPath float_start =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          navigation, glm::vec3(0.0f, 64.5f + 0.4f, 0.0f), body_goal, query);
  Expect(float_start.valid,
         "start floating within jumpHeight should snap to stand node");

  int waypoint_index = 0;
  const cutum::WaypointFollowResult follow = cutum::UWaypointFollower::Update(
      glm::vec3(0.0f, 64.5f, 0.0f), path, 0.45f, waypoint_index);
  Expect(glm::length(follow.move_dir) > 0.1f, "follower should emit direction");

  // Neighbor-cell snap: start stand invalid at (0,*), valid only at (1,*).
  class UEdgeNavigationMock : public cutum::IUWorldNavigation
  {
  public:
    bool IsTerrestrialStandNode(const cutum::NavigationStandNode &node,
                                float /*body_height*/) const override
    {
      return node.ground_y == 64 && node.x == 1 && node.z == 0;
    }
    bool CanStepTerrestrial(const cutum::NavigationStandNode & /*from*/,
                            const cutum::NavigationStandNode &to,
                            float /*max_jump*/, float /*max_drop*/,
                            float body_height) const override
    {
      return IsTerrestrialStandNode(to, body_height);
    }
  };
  UEdgeNavigationMock edge_nav;
  const cutum::NavigationPath edge_path =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          edge_nav, glm::vec3(0.2f, 64.5f, 0.0f), glm::vec3(1.0f, 64.5f, 0.0f),
          query);
  Expect(edge_path.valid,
         "start on invalid cell should snap to XZ neighbor stand node");

  std::cout << "navigation_pathfinder_test: OK" << std::endl;
  return 0;
}
