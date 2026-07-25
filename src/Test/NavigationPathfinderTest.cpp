#include "Navigation/UNavigationPathfinder.h"
#include "Navigation/UWaypointFollower.h"
#include "Navigation/NavigationPathBudget.h"
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

/// Flat stands everywhere, but cannot step onto x >= 3 (wall).
class UWalledNavigationMock : public cutum::IUWorldNavigation
{
public:
  bool IsTerrestrialStandNode(const cutum::NavigationStandNode &node,
                              float /*body_height*/) const override
  {
    return node.ground_y == 64;
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
    if (to.ground_y != 64 || to.x >= 3)
    {
      return false;
    }
    const float dy = static_cast<float>(to.ground_y - from.ground_y);
    return dy <= max_jump + 0.01f && dy >= -max_drop - 0.01f;
  }
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
  cutum::UNavigationPathBudget::SetExpandsPerTick(100000);
  cutum::UNavigationPathBudget::BeginActivityTick();

  UFlatNavigationMock navigation(64);
  cutum::NavigationQuery query;
  query.search_distance = 8;
  query.body_height = 1.8f;
  query.max_jump = 1.0f;

  const cutum::NavigationPath path = cutum::UNavigationPathfinder::FindTerrestrialPath(
      navigation, glm::vec3(0.0f, 64.5f, 0.0f), glm::vec3(3.0f, 64.5f, 0.0f),
      query);
  Expect(path.valid, "path on flat plane should succeed");
  Expect(!path.partial, "reachable goal should not be partial");
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

  // Partial path: goal beyond wall at x>=3.
  cutum::UNavigationPathBudget::BeginActivityTick();
  UWalledNavigationMock walled;
  const cutum::NavigationPath partial =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          walled, glm::vec3(0.0f, 64.5f, 0.0f), glm::vec3(5.0f, 64.5f, 0.0f),
          query);
  Expect(partial.valid, "blocked goal should yield partial corridor");
  Expect(partial.partial, "blocked goal path should be marked partial");
  Expect(partial.waypoints.size() >= 2, "partial path needs usable waypoints");
  Expect(partial.failReason == "partial", "partial reason tag");

  // 1-block climb out of a pit: climb-through must not treat landing solid as
  // headroom (regression for zombie/skeleton stuck in 1-deep holes).
  class USteppedNavigationMock : public cutum::IUWorldNavigation
  {
  public:
    bool IsTerrestrialStandNode(const cutum::NavigationStandNode &node,
                                float /*body_height*/) const override
    {
      // Pit floor at y=63 for x<=0; plateau at y=64 for x>=1.
      if (node.x <= 0)
      {
        return node.ground_y == 63;
      }
      return node.ground_y == 64;
    }
    bool CanStepTerrestrial(const cutum::NavigationStandNode &from,
                            const cutum::NavigationStandNode &to,
                            float max_jump, float max_drop,
                            float body_height) const override
    {
      const int dx = std::abs(to.x - from.x);
      const int dz = std::abs(to.z - from.z);
      if (dx + dz != 1 || !IsTerrestrialStandNode(to, body_height))
      {
        return false;
      }
      const float dy = static_cast<float>(to.ground_y - from.ground_y);
      if (dy > max_jump + 0.01f || dy < -max_drop - 0.01f)
      {
        return false;
      }
      // Correct climb-through: open cells strictly below landing ground_y.
      for (int y = from.ground_y + 1; y < to.ground_y; ++y)
      {
        (void)y;
        return false; // no mid-air solids in this mock
      }
      return true;
    }
  };
  cutum::UNavigationPathBudget::BeginActivityTick();
  USteppedNavigationMock stepped;
  const cutum::NavigationPath climb_path =
      cutum::UNavigationPathfinder::FindTerrestrialPath(
          stepped, glm::vec3(0.0f, 63.5f, 0.0f), glm::vec3(2.0f, 64.5f, 0.0f),
          query);
  Expect(climb_path.valid, "path should climb one block out of a pit");
  Expect(!climb_path.partial, "reachable plateau should not be partial");

  // Global expand budget: N parallel searches stay within cap.
  cutum::UNavigationPathBudget::SetExpandsPerTick(12);
  cutum::UNavigationPathBudget::BeginActivityTick();
  int searches = 0;
  for (int i = 0; i < 8; ++i)
  {
    cutum::NavigationQuery budget_query = query;
    budget_query.search_distance = 32;
    const cutum::NavigationPath budget_path =
        cutum::UNavigationPathfinder::FindTerrestrialPath(
            navigation, glm::vec3(0.0f, 64.5f, 0.0f),
            glm::vec3(20.0f, 64.5f, 0.0f), budget_query);
    ++searches;
    (void)budget_path;
  }
  Expect(searches == 8, "should attempt parallel searches");
  Expect(cutum::UNavigationPathBudget::GetExpandsUsed() <= 12,
         "expand use must not exceed tick budget");
  Expect(!cutum::UNavigationPathBudget::HasRemainingBudget() ||
             cutum::UNavigationPathBudget::GetExpandsUsed() > 0,
         "budget should be consumed across searches");

  cutum::UNavigationPathBudget::SetExpandsPerTick(
      cutum::UNavigationPathBudget::kDefaultExpandsPerTick);

  std::cout << "navigation_pathfinder_test: OK" << std::endl;
  return 0;
}
