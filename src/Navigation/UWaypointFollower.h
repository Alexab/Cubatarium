#ifndef UWAYPOINTFOLLOWER_H
#define UWAYPOINTFOLLOWER_H

#include "Navigation/NavigationTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

struct WaypointFollowResult
{
  glm::vec3 move_dir{0.0f};
  bool path_finished{false};
};

class UWaypointFollower
{
public:
  static WaypointFollowResult Update(const glm::vec3 &current_body_origin,
                                     const NavigationPath &path,
                                     float arrive_radius, int &waypoint_index);
};

} // namespace cutum

#endif
