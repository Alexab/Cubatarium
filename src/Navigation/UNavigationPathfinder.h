#ifndef UNAVIGATIONPATHFINDER_H
#define UNAVIGATIONPATHFINDER_H

#include "Navigation/IUWorldNavigation.h"
#include "Navigation/NavigationTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UNavigationPathfinder
{
public:
  static NavigationPath FindTerrestrialPath(const IUWorldNavigation &navigation,
                                            const glm::vec3 &start_body,
                                            const glm::vec3 &goal_body,
                                            const NavigationQuery &query);
};

} // namespace cutum

#endif
