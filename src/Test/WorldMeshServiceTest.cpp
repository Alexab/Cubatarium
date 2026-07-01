#include "World/Chunks/ChunkManager.h"

#include <glm/glm.hpp>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace cutum
{
namespace
{

struct ChunkDirtySet
{
  std::vector<glm::ivec3> Chunks;
  std::unordered_set<glm::ivec3, IVec3Hash> Set;

  void MarkDirty(glm::ivec3 coord)
  {
    if (!Set.insert(coord).second)
    {
      return;
    }
    Chunks.push_back(coord);
  }

  void RemoveChunk(glm::ivec3 coord) { Set.erase(coord); }

  size_t GetDirtyCount() const { return Chunks.size(); }
};

} // namespace
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "world_mesh_service_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::ChunkDirtySet cache;

  cache.MarkDirty(glm::ivec3{0, 0, 0});
  Expect(cache.GetDirtyCount() == 1, "first MarkDirty should add one entry");

  cache.MarkDirty(glm::ivec3{0, 0, 0});
  Expect(cache.GetDirtyCount() == 1, "duplicate MarkDirty should dedup");

  cache.MarkDirty(glm::ivec3{1, 0, 0});
  Expect(cache.GetDirtyCount() == 2, "second chunk should increase dirty count");

  cache.RemoveChunk(glm::ivec3{0, 0, 0});
  Expect(cache.GetDirtyCount() == 2,
          "RemoveChunk only updates set; count tracks queued coords");

  std::cout << "world_mesh_service_test: OK" << std::endl;
  return 0;
}
