#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
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
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kStone = 8;
  by_id[kLava] = lava;
  by_id[kWater] = water;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["lava"] = kLava;
  name_to_id["water"] = kWater;
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
  constexpr cutum::BlockId kWater = 9;
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
         "lava in open corner has side faces to air");

  cutum::UBlockWorld fluid_stone_world;
  fluid_stone_world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 fs_water(3, 10, 3);
  const glm::ivec3 fs_stone(2, 10, 3);
  fluid_stone_world.SetBlock(fs_water, kWater);
  fluid_stone_world.SetFluidState(fs_water, cutum::FluidCellState::Source());
  fluid_stone_world.SetBlock(fs_stone, kStone);
  const std::vector<cutum::GreedyQuad> fs_quads =
      cutum::UGreedyMesher::BuildChunkMesh(fluid_stone_world,
                                           glm::ivec3(0, 0, 0), registry);
  int fluid_faces_toward_stone = 0;
  int stone_faces_toward_fluid = 0;
  for (const cutum::GreedyQuad &quad : fs_quads)
  {
    if (quad.Id == kWater && quad.axis == 0 && quad.faceSign < 0 &&
        quad.slice == fs_water.x)
    {
      ++fluid_faces_toward_stone;
    }
    if (quad.Id == kStone && quad.axis == 0 && quad.faceSign > 0 &&
        quad.slice == fs_stone.x)
    {
      ++stone_faces_toward_fluid;
    }
  }
  Expect(fluid_faces_toward_stone == 0, "fluid hides face toward solid");
  Expect(stone_faces_toward_fluid >= 1, "solid keeps face toward fluid");

  // Cross-chunk: flowing level in neighbor chunk must not create a false wall.
  cutum::UBlockWorld cross_world;
  cross_world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 seam_a(cutum::CHUNK_SIZE - 1, 10, 8);
  const glm::ivec3 seam_b(cutum::CHUNK_SIZE, 10, 8);
  cross_world.SetBlock(seam_a, kWater);
  cross_world.SetFluidState(seam_a, cutum::FluidCellState::Source());
  cross_world.SetBlock(seam_b, kWater);
  cross_world.SetFluidState(seam_b, cutum::FluidCellState::Flowing(1));
  cross_world.SetBlock(seam_a + glm::ivec3(0, 1, 0), cutum::BLOCK_AIR);
  cross_world.SetBlock(seam_b + glm::ivec3(0, 1, 0), cutum::BLOCK_AIR);

  const std::vector<cutum::GreedyQuad> seam_quads =
      cutum::UGreedyMesher::BuildChunkMesh(cross_world, glm::ivec3(0, 0, 0),
                                           registry);
  int seam_internal_faces = 0;
  int seam_top_faces = 0;
  for (const cutum::GreedyQuad &quad : seam_quads)
  {
    if (quad.Id != kWater)
    {
      continue;
    }
    if (quad.axis == 1 && quad.faceSign > 0)
    {
      ++seam_top_faces;
    }
    if (quad.axis == 0 && quad.faceSign > 0 && quad.slice == seam_a.x)
    {
      ++seam_internal_faces;
    }
  }
  Expect(seam_top_faces >= 1, "cross-chunk water has top face at seam");
  Expect(seam_internal_faces == 0,
         "cross-chunk water hides internal face at chunk seam");

  cutum::GreedyQuad flowing_top;
  flowing_top.axis = 1;
  flowing_top.faceSign = 1;
  flowing_top.slice = 10;
  flowing_top.u = 5;
  flowing_top.v = 7;
  flowing_top.width = 1;
  flowing_top.height = 1;
  flowing_top.Id = kWater;
  flowing_top.FluidPacked =
      cutum::PackFluidCellState(cutum::FluidCellState::Flowing(3));
  std::vector<cutum::GreedyMeshVertex> flowing_vertices;
  std::vector<uint32_t> flowing_indices;
  cutum::AppendGreedyQuad(flowing_top, glm::ivec3(0, 0, 0), flowing_vertices,
                          flowing_indices);
  Expect(!flowing_vertices.empty(), "flowing top emits vertices");
  Expect(flowing_vertices[0].px > 6.4f && flowing_vertices[0].px < 6.6f,
         "flowing top face uses quad v position");
  Expect(flowing_vertices[0].pz > 4.4f && flowing_vertices[0].pz < 4.6f,
         "flowing top face uses quad u position");

  // Water above solid must still emit top faces (max_mesh_y includes fluid).
  cutum::UBlockWorld pit_world;
  pit_world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 pit_water(5, 12, 5);
  const glm::ivec3 pit_floor(5, 11, 5);
  pit_world.SetBlock(pit_floor, kStone);
  pit_world.SetBlock(pit_water, kWater);
  pit_world.SetFluidState(pit_water, cutum::FluidCellState::Flowing(3));
  pit_world.SetBlock(pit_water + glm::ivec3(0, 1, 0), cutum::BLOCK_AIR);
  for (const glm::ivec3 &offset : {glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
                                   glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1)})
  {
    pit_world.SetBlock(pit_water + offset, kStone);
  }
  const std::vector<cutum::GreedyQuad> pit_quads =
      cutum::UGreedyMesher::BuildChunkMesh(pit_world, glm::ivec3(0, 0, 0),
                                           registry);
  Expect(CountFluidFaces(pit_quads, kWater, true) >= 1,
         "water above stone has top face");
  Expect(CountFluidFaces(pit_quads, kWater, false) == 0,
         "walled pool hides fluid faces toward stone");

  cutum::UBlockWorld open_world;
  open_world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 open_water(8, 10, 8);
  open_world.SetBlock(open_water, kWater);
  open_world.SetFluidState(open_water, cutum::FluidCellState::Flowing(3));
  const std::vector<cutum::GreedyQuad> open_quads =
      cutum::UGreedyMesher::BuildChunkMesh(open_world, glm::ivec3(0, 0, 0),
                                           registry);
  Expect(CountFluidFaces(open_quads, kWater, false) >= 4,
         "open water block keeps side faces to air");

  cutum::UBlockWorld shore_mesh_world;
  shore_mesh_world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 shore_water(12, 10, 8);
  const glm::ivec3 shore_air(11, 10, 8);
  shore_mesh_world.SetBlock(shore_water, kWater);
  shore_mesh_world.SetFluidState(shore_water, cutum::FluidCellState::Source());
  const std::vector<cutum::GreedyQuad> shore_quads =
      cutum::UGreedyMesher::BuildChunkMesh(shore_mesh_world,
                                           glm::ivec3(0, 0, 0), registry);
  int shore_faces_toward_air = 0;
  for (const cutum::GreedyQuad &quad : shore_quads)
  {
    if (quad.Id == kWater && quad.axis == 0 && quad.faceSign < 0 &&
        quad.slice == shore_water.x)
    {
      ++shore_faces_toward_air;
    }
  }
  Expect(shore_faces_toward_air >= 1,
         "water at shore keeps vertical face toward dug air");

  std::cout << "fluid_mesh_faces_test: OK" << std::endl;
  return 0;
}
