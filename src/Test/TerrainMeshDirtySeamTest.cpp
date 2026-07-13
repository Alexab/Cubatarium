#include "World/Chunks/ChunkManager.h"
#include "World/Math/GridMath.h"

#include <glm/glm.hpp>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace
{

using CoordSet = std::unordered_set<glm::ivec3, cutum::IVec3Hash>;

void CollectTerrainMeshDirtySeamed(glm::ivec3 ground_chunk_coord, int min_y,
                                   int max_y, bool include_horizontal_neighbors,
                                   CoordSet &out)
{
  const int cy0 = cutum::FloorDiv(min_y, cutum::CHUNK_SIZE);
  const int cy1 = cutum::FloorDiv(max_y, cutum::CHUNK_SIZE);
  const int cx0 =
      include_horizontal_neighbors ? ground_chunk_coord.x - 1
                                 : ground_chunk_coord.x;
  const int cx1 =
      include_horizontal_neighbors ? ground_chunk_coord.x + 1
                                 : ground_chunk_coord.x;
  const int cz0 =
      include_horizontal_neighbors ? ground_chunk_coord.z - 1
                                 : ground_chunk_coord.z;
  const int cz1 =
      include_horizontal_neighbors ? ground_chunk_coord.z + 1
                                 : ground_chunk_coord.z;
  for (int cx = cx0; cx <= cx1; ++cx)
  {
    for (int cz = cz0; cz <= cz1; ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        out.insert(glm::ivec3(cx, cy, cz));
      }
    }
  }
}

void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "terrain_mesh_dirty_seam_test: " << message << std::endl;
    std::exit(1);
  }
}

} // namespace

int main()
{
  constexpr int sea = 48;
  const glm::ivec3 ground{10, 0, 12};
  CoordSet seamed;
  CollectTerrainMeshDirtySeamed(ground, sea, sea, true, seamed);
  Expect(seamed.count(glm::ivec3(10, 3, 12)) == 1, "center slice at sea");
  Expect(seamed.count(glm::ivec3(9, 3, 12)) == 1, "west seam neighbor");
  Expect(seamed.count(glm::ivec3(10, 0, 12)) == 0,
          "sea-only range should not include cy=0");

  CoordSet narrow;
  CollectTerrainMeshDirtySeamed(ground, sea, sea, false, narrow);
  Expect(narrow.size() == 1, "no neighbors should mark one chunk slice");
  Expect(narrow.count(glm::ivec3(10, 3, 12)) == 1, "single cy at sea chunk");

  CoordSet wide_y;
  CollectTerrainMeshDirtySeamed(ground, 0, sea + cutum::CHUNK_SIZE * 2, true,
                                wide_y);
  const int cy0 = 0;
  const int cy1 = cutum::FloorDiv(sea + cutum::CHUNK_SIZE * 2, cutum::CHUNK_SIZE);
  const size_t expected_slices = static_cast<size_t>(cy1 - cy0 + 1);
  Expect(wide_y.count(glm::ivec3(10, 3, 12)) == 1, "wide y includes sea slice");
  size_t center_count = 0;
  for (const glm::ivec3 &coord : wide_y)
  {
    if (coord.x == ground.x && coord.z == ground.z)
    {
      ++center_count;
    }
  }
  Expect(center_count == expected_slices, "center column gets all cy slices");

  std::cout << "terrain_mesh_dirty_seam_test: OK" << std::endl;
  return 0;
}
