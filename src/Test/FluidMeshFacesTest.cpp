#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_mesh_faces_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  lava.Render.Transparent = true;
  lava.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kStone = 8;
  by_id[kLava] = lava;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["lava"] = kLava;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static int CountFluidFaces(const std::vector<cutum::GreedyQuad> &quads,
                           cutum::BlockId fluid_id, bool top_only)
{
  int count = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id != fluid_id)
    {
      continue;
    }
    if (top_only)
    {
      if (quad.axis == 1 && quad.faceSign > 0)
      {
        ++count;
      }
    }
    else if (!(quad.axis == 1 && quad.faceSign > 0))
    {
      ++count;
    }
  }
  return count;
}

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kStone = 8;
  const glm::ivec3 base(1, 10, 1);
  for (int dx = 0; dx < 2; ++dx)
  {
    for (int dz = 0; dz < 2; ++dz)
    {
      for (int dy = 0; dy < 2; ++dy)
      {
        const glm::ivec3 pos = base + glm::ivec3(dx, dy, dz);
        if (dx == 0 && dz == 0 && dy == 0)
        {
          world.SetBlock(pos, kLava);
          world.SetFluidState(pos, cutum::FluidCellState::Source());
        }
        else if (dy == 0)
        {
          world.SetBlock(pos, kStone);
        }
      }
    }
  }

  const cutum::ChunkMeshSnapshot snapshot =
      cutum::ChunkMeshSnapshot::Capture(world, glm::ivec3(0, 0, 0), 1);
  Expect(snapshot.GetFluid(base).IsSource(), "snapshot captures fluid layer");

  const std::vector<cutum::GreedyQuad> quads =
      cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry);
  Expect(CountFluidFaces(quads, kLava, true) >= 1, "lava pit has top face");
  Expect(CountFluidFaces(quads, kLava, false) >= 1,
         "lava corner has side faces in basin");

  std::cout << "fluid_mesh_faces_test: OK" << std::endl;
  return 0;
}
