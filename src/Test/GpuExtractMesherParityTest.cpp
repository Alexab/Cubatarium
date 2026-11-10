// The GPU/CPU face-extract path must place faces exactly where the CPU mesher
// does. Regression: the GPU greedy kernel stored the plane index (voxel+1 for
// + faces) in rect.slice while every emitter treats slice as the voxel
// coordinate (corner = slice +/- 0.5), so every + face of a GPU-meshed chunk
// floated one block too far (+Y tops one block high, +X/+Z walls shifted),
// producing see-through slits at steps and stale-looking surfaces.
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "gpu_extract_mesher_parity_test: " << message << std::endl;
    std::exit(1);
  }
}

static constexpr cutum::BlockId kStone = 8;

using Cell = std::tuple<int, int, int, int, int>; // axis, sign, x, y, z

static std::set<Cell> Cells(const std::vector<cutum::GreedyQuad> &quads)
{
  std::set<Cell> out;
  for (const cutum::GreedyQuad &q : quads)
  {
    const int u_axis = (q.axis + 1) % 3;
    const int v_axis = (q.axis + 2) % 3;
    for (int dv = 0; dv < q.height; ++dv)
    {
      for (int du = 0; du < q.width; ++du)
      {
        int p[3];
        p[q.axis] = q.slice;
        p[u_axis] = q.u + du;
        p[v_axis] = q.v + dv;
        out.emplace(q.axis, q.faceSign, p[0], p[1], p[2]);
      }
    }
  }
  return out;
}

int main()
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
  cutum::UBlockRegistry registry(nullptr, definitions);

  // Stepped, pseudo-random terrain inside chunk (2,4,3); neighbor columns
  // resident (lower slice) so border faces are emitted by both paths.
  cutum::UBlockWorld world;
  uint32_t rng = 12345u;
  const glm::ivec3 coord(2, 4, 3);
  const glm::ivec3 origin = coord * cutum::CHUNK_SIZE;
  for (int x = -1; x <= cutum::CHUNK_SIZE; ++x)
  {
    for (int z = -1; z <= cutum::CHUNK_SIZE; ++z)
    {
      rng = rng * 1664525u + 1013904223u;
      const int h = 1 + static_cast<int>((rng >> 16) % 9u);
      for (int y = 0; y < h; ++y)
      {
        world.SetBlock(origin + glm::ivec3(x, y, z), kStone);
      }
    }
  }
  const cutum::ChunkMeshSnapshot snap =
      cutum::ChunkMeshSnapshot::Capture(world, coord, 1);
  Expect(cutum::SnapshotIsGpuExtractEligible(snap, registry),
         "opaque stone chunk must be extract-eligible");

  const auto cpu = cutum::UGreedyMesher::BuildChunkMesh(snap, registry);
  const auto extracted = cutum::ExtractOpaqueFacesCpu(snap, registry);
  const auto merged = cutum::MergeOpaqueQuadsStrict(extracted);
  const std::set<Cell> cpu_cells = Cells(cpu);
  Expect(!cpu_cells.empty(), "terrain must produce faces");
  Expect(Cells(extracted) == cpu_cells,
         "ExtractOpaqueFacesCpu face cells differ from the CPU mesher");
  Expect(Cells(merged) == cpu_cells,
         "merged extract face cells differ from the CPU mesher");

  // Shader contract: the greedy kernel must publish voxel-coordinate slices.
  std::ifstream in(CUBATARIUM_GPU_EMIT_SOURCE_PATH);
  Expect(in.good(), "cannot open GpuGreedyOpaqueEmit.cpp");
  std::stringstream ss;
  ss << in.rdbuf();
  Expect(ss.str().find("rects[idx].slice = faceSign > 0 ? sliceU - 1u : sliceU;") !=
             std::string::npos,
         "greedy kernel must store the voxel slice (plane index - 1 for + faces)");

  std::cout << "gpu_extract_mesher_parity_test OK cells=" << cpu_cells.size()
            << std::endl;
  return 0;
}
