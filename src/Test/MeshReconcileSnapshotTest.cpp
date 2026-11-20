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
  // Even a growing/shrinking live cache must not restart the bounded pass.
  // Its stable tail must be serviced before newly arrived keys join a pass.
  meshes.clear();
  meshes[old] = 1;
  meshes[kept] = 1;
  keys.clear();
  cursor = 0;
  cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor);
  const auto stable_keys = keys;
  cursor = 1;
  for (int tick = 0; tick < 20; ++tick)
  {
    meshes[{10 + tick, -3, 0}] = 1;
    Expect(!cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor) &&
               keys == stable_keys && cursor == 1,
           "cache growth must not restart a partially consumed pass");
  }
  meshes.erase(old);
  Expect(!cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor) &&
             keys == stable_keys && cursor == 1,
         "cache shrink must not discard the unvisited snapshot tail");
  cursor = keys.size();
  Expect(cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor) &&
             keys.size() == meshes.size() && cursor == 0 &&
             std::find(keys.begin(), keys.end(), old) == keys.end(),
         "the next pass incorporates every retained arrival and omits retired keys");
  meshes.clear();
  for (int i = 0; i < 32; ++i)
    meshes[{i, -3, 0}] = 1;
  keys.clear();
  cursor = 0;
  cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor);
  const auto initial_pass = keys;
  std::vector<glm::ivec3> visited;
  for (int frame = 0; frame < 32; ++frame)
  {
    meshes[{100 + frame, -3, 0}] = 1;
    cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor);
    // Match the production reconciliation consumer's one-check frame budget.
    visited.push_back(keys[cursor++]);
  }
  Expect(visited == initial_pass,
         "continuous arrivals must not starve any key in a bounded pass");
  cutum::RefreshMeshReconcileSnapshot(meshes, keys, cursor);
  Expect(keys.size() == 64 && cursor == 0,
         "every queued arrival joins the immediately following pass");
  return EXIT_SUCCESS;
}
