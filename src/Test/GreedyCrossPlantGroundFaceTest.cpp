#include "Test/FluidTestHelpers.h"

#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "greedy_cross_plant_ground_face_test: " << message
              << std::endl;
    std::exit(1);
  }
}

int main()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kDirt = 8;
  constexpr cutum::BlockId kPlant = 9;
  constexpr cutum::BlockId kGlass = 10;
  cutum::BlockDefinition dirt;
  dirt.Name = "dirt";
  dirt.Physics = cutum::BlockPhysicsProfile::Solid();
  // Mirrors resource_packs/*/blocks/mtg_grass_1.json:
  // render {transparent: true, style: cross}, movement occupancy 0.
  cutum::BlockDefinition plant;
  plant.Name = "mtg_grass_1";
  plant.Physics = cutum::BlockPhysicsProfile::Solid();
  plant.Physics.Movement.Occupancy = 0.0f;
  plant.Render.Transparent = true;
  plant.Render.Style = cutum::BlockRenderStyle::Cross;
  cutum::BlockDefinition glass;
  glass.Name = "glass";
  glass.Physics = cutum::BlockPhysicsProfile::Solid();
  glass.Render.Transparent = true;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kDirt] = dirt;
  by_id[kPlant] = plant;
  by_id[kGlass] = glass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["dirt"] = kDirt;
  name_to_id["mtg_grass_1"] = kPlant;
  name_to_id["glass"] = kGlass;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  cutum::UBlockRegistry registry(nullptr, definitions);

  const auto top_face_covers = [](const std::vector<cutum::GreedyQuad> &quads,
                                  cutum::BlockId id, int x, int y, int z)
  {
    for (const cutum::GreedyQuad &quad : quads)
    {
      if (quad.Id != id || quad.axis != 1 || quad.faceSign <= 0)
      {
        continue;
      }
      // quad.slice is the layer of the face; u/v extents cover x/z.
      if (quad.slice == y + 1 || quad.slice == y)
      {
        // Axis-1 quads use (u, v) = (z, x).
        if (z >= quad.u && z < quad.u + quad.width && x >= quad.v &&
            x < quad.v + quad.height)
        {
          return true;
        }
      }
    }
    return false;
  };

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int x = 4; x < 9; ++x)
  {
    for (int z = 4; z < 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), kDirt);
    }
  }
  world.SetBlock(glm::ivec3(6, 11, 6), kPlant);
  const auto quads = cutum::UGreedyMesher::BuildChunkMesh(
      world, glm::ivec3(0, 0, 0), registry);
  int dirt_top_quads = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id == kDirt && quad.axis == 1 && quad.faceSign > 0)
    {
      ++dirt_top_quads;
    }
  }
  std::cout << "dirt top quads=" << dirt_top_quads << " total=" << quads.size()
            << std::endl;
  Expect(dirt_top_quads >= 1, "dirt has top faces");
  Expect(top_face_covers(quads, kDirt, 6, 10, 6),
         "dirt top face under a cross plant is meshed");
  // Control: glass above dirt (opaque face toward transparent solid).
  cutum::UBlockWorld glass_world;
  glass_world.SetFluidDefinitions(definitions.get());
  for (int x = 4; x < 9; ++x)
  {
    for (int z = 4; z < 9; ++z)
    {
      glass_world.SetBlock(glm::ivec3(x, 10, z), kDirt);
    }
  }
  glass_world.SetBlock(glm::ivec3(6, 11, 6), kGlass);
  const auto glass_quads = cutum::UGreedyMesher::BuildChunkMesh(
      glass_world, glm::ivec3(0, 0, 0), registry);
  std::cout << "glass control total=" << glass_quads.size() << std::endl;
  // Exposed-face audit on a beach-like staircase: every solid face toward air
  // must be covered by exactly the emitted quads.
  {
    cutum::UBlockWorld stairs;
    stairs.SetFluidDefinitions(definitions.get());
    int heights[16][16];
    uint32_t state = 12345u;
    for (int z = 0; z < 16; ++z)
    {
      for (int x = 0; x < 16; ++x)
      {
        state = state * 1664525u + 1013904223u;
        const int jitter = static_cast<int>((state >> 16U) % 3U) - 1;
        const int h = std::clamp(2 + z / 2 + (jitter > 0 ? 1 : 0), 1, 14);
        heights[z][x] = h;
        for (int y = 0; y <= h; ++y)
        {
          stairs.SetBlock(glm::ivec3(x, y, z), kDirt);
        }
      }
    }
    const auto quad_list = cutum::UGreedyMesher::BuildChunkMesh(
        stairs, glm::ivec3(0, 0, 0), registry);
    const auto covered = [&](int axis, int sign, glm::ivec3 p)
    {
      int u = 0;
      int v = 0;
      if (axis == 0)
      {
        u = p.y;
        v = p.z;
      }
      else if (axis == 1)
      {
        u = p.z;
        v = p.x;
      }
      else
      {
        u = p.x;
        v = p.y;
      }
      const int slice = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
      for (const cutum::GreedyQuad &quad : quad_list)
      {
        if (quad.axis == axis && quad.faceSign == sign && quad.slice == slice &&
            u >= quad.u && u < quad.u + quad.width && v >= quad.v &&
            v < quad.v + quad.height)
        {
          return true;
        }
      }
      return false;
    };
    int expected = 0;
    int missing = 0;
    for (int z = 0; z < 16; ++z)
    {
      for (int x = 0; x < 16; ++x)
      {
        for (int y = 0; y <= heights[z][x]; ++y)
        {
          const glm::ivec3 p(x, y, z);
          for (int axis = 0; axis < 3; ++axis)
          {
            for (int sign = -1; sign <= 1; sign += 2)
            {
              glm::ivec3 n = p;
              n[axis] += sign;
              const bool in_chunk = n.x >= 0 && n.x < 16 && n.z >= 0 &&
                                    n.z < 16 && n.y >= 0;
              if (in_chunk && stairs.GetBlock(n) != cutum::BLOCK_AIR)
              {
                continue;
              }
              if (!in_chunk && n.y >= 0)
              {
                continue; // chunk border depends on neighbors (unloaded)
              }
              if (n.y < 0)
              {
                continue; // world floor
              }
              ++expected;
              if (!covered(axis, sign, p))
              {
                if (missing < 8)
                {
                  std::cout << "MISSING face block=(" << x << ',' << y << ','
                            << z << ") axis=" << axis << " sign=" << sign
                            << std::endl;
                }
                ++missing;
              }
            }
          }
        }
      }
    }
    std::cout << "staircase expected faces=" << expected
              << " missing=" << missing << std::endl;
    Expect(missing == 0, "greedy mesher emits every exposed staircase face");
  }
  std::cout << "greedy_cross_plant_ground_face_test: OK" << std::endl;
  return 0;
}
