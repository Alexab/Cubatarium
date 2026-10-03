#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Unified result of applying a relight merge to resident chunks (M03 / A06).
/// Remesh decisions must derive from this set, not from an independent bool.
struct LightChangeSet
{
  std::vector<glm::ivec3> changed_coords;
  bool any_changed() const { return !changed_coords.empty(); }

  void Add(glm::ivec3 coord)
  {
    changed_coords.push_back(coord);
  }

  void Clear()
  {
    changed_coords.clear();
  }
};

} // namespace cutum
