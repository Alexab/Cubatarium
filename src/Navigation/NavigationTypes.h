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
  /// Empty when valid; otherwise start_invalid|goal_invalid|search_exhausted.
  std::string failReason;
};

struct NavigationQuery
{
  int search_distance{32};
  float max_jump{1.0f};
  float max_drop{3.0f};
  float body_height{1.8f};
};

} // namespace cutum

#endif
