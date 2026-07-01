#ifndef WORLDNAVIGATIONQUERIES_H
#define WORLDNAVIGATIONQUERIES_H

#include "Navigation/IUWorldNavigation.h"

namespace cutum
{

class UWorld;

NavigationStandNode NavigationStandNodeFromBody(const glm::vec3 &body_origin);

class UWorldNavigationQueries : public IUWorldNavigation
{
public:
  explicit UWorldNavigationQueries(const UWorld &world);

  bool IsTerrestrialStandNode(const NavigationStandNode &node,
                              float body_height) const override;
  bool CanStepTerrestrial(const NavigationStandNode &from,
                          const NavigationStandNode &to, float max_jump,
                          float max_drop, float body_height) const override;

private:
  const UWorld &World;
};

} // namespace cutum

#endif
