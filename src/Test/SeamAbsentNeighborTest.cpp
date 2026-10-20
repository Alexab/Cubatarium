// Chunk-border faces toward an absent neighbor slice.
// Terrain columns only materialize slices up to their highest non-air slice,
// so at a height step across a chunk border the shorter column has no slice at
// the taller column's cy. The mesher used to treat that as Unknown and hide the
// wall (see-through slits). A column with NO resident slice stays Unknown.
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "seam_absent_neighbor_test: " << message << std::endl;
    std::exit(1);
  }
}

static constexpr cutum::BlockId kStone = 8;

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static int CountPlusXFacesAtBorder(const std::vector<cutum::GreedyQuad> &quads)
{
  int n = 0;
  for (const cutum::GreedyQuad &q : quads)
  {
    // axis 0, +X face on the last slice of the chunk (local x = 15).
    if (q.axis == 0 && q.faceSign > 0 && q.slice == cutum::CHUNK_SIZE - 1)
    {
      ++n;
    }
  }
  return n;
}

static int BorderFaces(bool neighbor_column_resident)
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  // Chunk (0,4,0) spans y 64..79; its +X border column is x=15.
  world.SetBlock(glm::ivec3(15, 64, 5), kStone);
  world.SetBlock(glm::ivec3(15, 65, 5), kStone);
  if (neighbor_column_resident)
  {
    // Column (1,*,0) is generated, but only up to cy=3 (y 48..63).
    world.SetBlock(glm::ivec3(16, 50, 5), kStone);
  }
  const glm::ivec3 coord(0, 4, 0);
  Expect(world.GetChunkManager().GetChunk(coord) != nullptr,
         "test chunk must exist");
  Expect(world.GetChunkManager().GetChunk(glm::ivec3(1, 4, 0)) == nullptr,
         "neighbor slice cy=4 must be absent");
  const cutum::ChunkMeshSnapshot snapshot =
      cutum::ChunkMeshSnapshot::Capture(world, coord, 1);
  const auto quads = cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry);
  // The live-world reader must agree with the snapshot reader.
  const auto live_quads =
      cutum::UGreedyMesher::BuildChunkMesh(world, coord, registry);
  Expect(CountPlusXFacesAtBorder(quads) == CountPlusXFacesAtBorder(live_quads),
         "snapshot and live-world mesh must agree on border faces");
  return CountPlusXFacesAtBorder(quads);
}

int main()
{
  Expect(BorderFaces(true) >= 1,
         "wall toward an absent slice of a resident column must be emitted");
  Expect(BorderFaces(false) == 0,
         "column that is not resident at all stays Unknown (no closing wall)");
  std::cout << "seam_absent_neighbor_test OK" << std::endl;
  return 0;
}
