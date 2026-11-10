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
  /// `reject_fluid_nodes` (default off, nav-hot path) additionally rejects
  /// stands whose body column touches fluid — set it for terrestrial movers
  /// so A* chase/flee paths never route across water (the post-motor habitat
  /// gate used to revert the walk there, leaving the mob stranded at the
  /// shore or pushed in by feelers).
  virtual bool IsTerrestrialStandNode(const NavigationStandNode &node,
                                      float body_height,
                                      float body_width = 0.6f,
                                      bool reject_fluid_nodes = false) const = 0;
  virtual bool CanStepTerrestrial(const NavigationStandNode &from,
                                  const NavigationStandNode &to,
                                  float max_jump, float max_drop,
                                  float body_height,
                                  float body_width = 0.6f,
                                  bool reject_fluid_nodes = false) const = 0;
};

} // namespace cutum

#endif
