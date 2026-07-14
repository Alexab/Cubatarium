#ifndef IUWORLDNAVIGATION_H
#define IUWORLDNAVIGATION_H

#include <glm/glm.hpp>

namespace cutum
{

/// Stand node: solid at (x, ground_y, z), feet on top of that block.
struct NavigationStandNode
{
  int x{0};
  int ground_y{0};
  int z{0};
};

class IUWorldNavigation
{
public:
  virtual ~IUWorldNavigation() = default;
  virtual bool IsTerrestrialStandNode(const NavigationStandNode &node,
                                      float body_height) const = 0;
  virtual bool CanStepTerrestrial(const NavigationStandNode &from,
                                  const NavigationStandNode &to,
                                  float max_jump, float max_drop,
                                  float body_height) const = 0;
};

} // namespace cutum

#endif
