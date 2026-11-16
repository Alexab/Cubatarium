#include "Render/Mesh/MeshReconcileSnapshot.h"
#include "World/Chunks/ChunkManager.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

int main()
{
  std::unordered_map<glm::ivec3, int, cutum::IVec3Hash> meshes;
  const glm::ivec3 old(0, -3, 0), kept(1, -3, 0), arrived(2, -3, 0);
  meshes[old] = 1;
  meshes[kept] = 1;
  std::vector<glm::ivec3> keys;
  size_t cursor = 0;
  Expect(cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor),
         "initial snapshot captured");
  cursor = 1;
  meshes.erase(old);
  meshes[arrived] = 1;
  Expect(!cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor),
         "a partial pass must not restart and starve its tail");
  cursor = keys.size();
  Expect(cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor),
         "completed pass refreshes membership even at unchanged cache size");
  Expect(cursor == 0 &&
             std::find(keys.begin(), keys.end(), arrived) != keys.end() &&
             std::find(keys.begin(), keys.end(), old) == keys.end(),
         "newly streamed meshes are serviced on the next pass");
  meshes.clear();
  Expect(cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor) &&
             keys.empty(),
         "clear retires stale keys");
  meshes[arrived] = 2;
  Expect(cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor) &&
             keys.size() == 1,
         "new world membership is captured after clear");
  return EXIT_SUCCESS;
}
