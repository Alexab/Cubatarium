#ifndef NAVIGATIONTYPES_H
#define NAVIGATIONTYPES_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace cutum
{

struct NavigationWaypoint
{
  glm::vec3 position{0.0f};
};

struct NavigationPath
{
  std::vector<NavigationWaypoint> waypoints;
  bool valid{false};
  /// True when corridor is closest-to-goal (goal unreachable / budget cut).
  bool partial{false};
  /// Empty when valid full path; start_invalid|goal_invalid|search_exhausted|
  /// budget_exhausted when invalid; "partial" when valid+partial.
  std::string failReason;
};

struct NavigationQuery
{
  int search_distance{32};
  float max_jump{1.0f};
  float max_drop{3.0f};
  float body_height{1.8f};
  /// Soft per-search expand cap (0 = default local max). Global tick budget
  /// still applies via UNavigationPathBudget.
  int max_expands{0};
};

} // namespace cutum

#endif
