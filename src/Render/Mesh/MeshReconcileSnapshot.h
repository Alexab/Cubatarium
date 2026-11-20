#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Keep a stable key snapshot for each bounded pass. Refresh after a complete
/// pass even if streaming replaced entries without changing the map's size.
/// Live growth/shrink also waits for the tail; retired keys are skipped by the
/// consumer. An empty cache retires the snapshot immediately.
template <typename MeshMap>
bool RefreshMeshReconcileSnapshot(const MeshMap &meshes,
                                  std::vector<glm::ivec3> &keys, size_t &cursor)
{
  if (!meshes.empty() && cursor < keys.size())
    return false;
  keys.clear();
  keys.reserve(meshes.size());
  for (const auto &entry : meshes)
    keys.push_back(entry.first);
  cursor = 0;
  return true;
}

} // namespace cutum
