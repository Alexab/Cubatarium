#include "World/Collision/VoxelDdaTraversal.h"

#include <cstdlib>
#include <iostream>
#include <unordered_set>

struct CellHash
{
  size_t operator()(const glm::ivec3 &v) const
  {
    size_t hash = static_cast<size_t>(v.x * 73856093);
    hash ^= static_cast<size_t>(v.y * 19349663);
    hash ^= static_cast<size_t>(v.z * 83492791);
    return hash;
  }
};

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "voxel_dda_traversal_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  std::unordered_set<glm::ivec3, CellHash> solids;
  solids.insert(glm::ivec3(3, 0, 0));

  const bool hit = cutum::TraverseVoxelRay(
      glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f,
      [&](glm::ivec3 cell) { return solids.count(cell) != 0; });
  Expect(hit, "ray expected to hit solid cell");

  const bool miss = cutum::TraverseVoxelRay(
      glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.0f, 1.0f, 0.0f), 2.0f,
      [&](glm::ivec3 cell) { return solids.count(cell) != 0; });
  Expect(!miss, "ray expected to miss");

  std::cout << "voxel_dda_traversal_test: OK" << std::endl;
  return 0;
}
