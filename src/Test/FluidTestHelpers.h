#pragma once

#include <glm/glm.hpp>

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"
#include "World/Physics/PhysicsProfile.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace FluidTest
{

inline void Expect(bool cond, const char *test_name, const char *message)
{
  if (!cond)
  {
    std::cerr << test_name << ": " << message << std::endl;
    std::exit(1);
  }
}

inline std::shared_ptr<cutum::UBlockDefinitionStorage> MakeTestFluidDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

inline std::shared_ptr<cutum::UBlockDefinitionStorage>
MakeTestFluidDecorDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition grass;
  grass.Name = "tall_grass";
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  grass.Physics.Movement.Occupancy = 0.0f;
  grass.Render.Style = cutum::BlockRenderStyle::Cross;
  grass.Render.Transparent = true;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kTallGrass] = grass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["tall_grass"] = kTallGrass;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

inline std::shared_ptr<cutum::UBlockDefinitionStorage>
MakeTestWaterLavaDecorDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kTallGrass = 10;
  constexpr cutum::BlockId kLava = 11;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  lava.Render.Transparent = true;
  lava.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition grass;
  grass.Name = "tall_grass";
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  grass.Physics.Movement.Occupancy = 0.0f;
  grass.Render.Style = cutum::BlockRenderStyle::Cross;
  grass.Render.Transparent = true;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kLava] = lava;
  by_id[kTallGrass] = grass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["lava"] = kLava;
  name_to_id["tall_grass"] = kTallGrass;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

struct FluidCellExpectation
{
  glm::ivec3 Pos;
  bool ExpectWaterBlock{false};
  int MinTopFaces{0};
  int MinSideFacesToAir{0};
  bool ExpectPlaceable{false};
  bool ExpectWaterlogged{false};
  cutum::BlockId DecorBlock{cutum::BLOCK_AIR};
};

inline int CountTopFacesAt(const std::vector<cutum::GreedyQuad> &quads,
                           cutum::BlockId fluid_id, glm::ivec3 world_pos,
                           glm::ivec3 chunk_coord)
{
  const glm::ivec3 local = world_pos - chunk_coord * cutum::CHUNK_SIZE;
  int count = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id != fluid_id || quad.axis != 1 || quad.faceSign <= 0 ||
        quad.slice != local.y)
    {
      continue;
    }
    if (quad.v <= local.x && local.x < quad.v + quad.height && quad.u <= local.z &&
        local.z < quad.u + quad.width)
    {
      ++count;
    }
  }
  return count;
}

inline int CountVerticalSideFacesAt(const std::vector<cutum::GreedyQuad> &quads,
                                    cutum::BlockId fluid_id, glm::ivec3 world_pos,
                                    glm::ivec3 chunk_coord)
{
  const glm::ivec3 local = world_pos - chunk_coord * cutum::CHUNK_SIZE;
  int count = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id != fluid_id || quad.axis == 1)
    {
      continue;
    }
    if (quad.axis == 0 && quad.slice == local.x)
    {
      if (quad.u <= local.y && local.y < quad.u + quad.width && quad.v <= local.z &&
          local.z < quad.v + quad.height)
      {
        ++count;
      }
    }
    if (quad.axis == 2 && quad.slice == local.z)
    {
      if (quad.u <= local.x && local.x < quad.u + quad.width && quad.v <= local.y &&
          local.y < quad.v + quad.height)
      {
        ++count;
      }
    }
  }
  return count;
}

inline std::vector<cutum::GreedyQuad>
BuildFluidMesh(const cutum::UBlockWorld &world, cutum::UBlockRegistry &registry,
               glm::ivec3 world_pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(world_pos);
  const cutum::ChunkMeshSnapshot snapshot =
      cutum::ChunkMeshSnapshot::Capture(world, chunk_coord, 1);
  return cutum::UGreedyMesher::BuildChunkMesh(snapshot, registry);
}

inline void ExpectFluidCells(const char *test_name, const cutum::UBlockWorld &world,
                             cutum::UBlockRegistry &registry,
                             cutum::BlockId fluid_id,
                             const std::vector<FluidCellExpectation> &cells)
{
  std::unordered_map<std::string, std::vector<cutum::GreedyQuad>> mesh_cache;
  for (const FluidCellExpectation &cell : cells)
  {
    const bool is_air = world.IsAir(cell.Pos);
    const cutum::BlockId block_id = world.GetBlock(cell.Pos);
    const bool is_waterlogged =
        cell.ExpectWaterlogged && registry.IsFluidPermeable(block_id) &&
        cutum::PackFluidCellState(world.GetFluidState(cell.Pos)) != 0;
    if (cell.ExpectWaterlogged)
    {
      Expect(is_waterlogged, test_name, "waterlogged decor cell mismatch");
    }
    else
    {
      const bool is_water = block_id == fluid_id;
      Expect(is_water == cell.ExpectWaterBlock, test_name,
             "fluid cell block id mismatch");
    }
    Expect(is_air == cell.ExpectPlaceable, test_name,
           "fluid cell placement mismatch");
    if (!cell.ExpectWaterBlock && !cell.ExpectWaterlogged)
    {
      continue;
    }
    const glm::ivec3 chunk_coord =
        cutum::UChunkManager::WorldToChunk(cell.Pos);
    const std::string key = std::to_string(chunk_coord.x) + "," +
                            std::to_string(chunk_coord.y) + "," +
                            std::to_string(chunk_coord.z);
    if (mesh_cache.find(key) == mesh_cache.end())
    {
      mesh_cache[key] = BuildFluidMesh(world, registry, cell.Pos);
    }
    const std::vector<cutum::GreedyQuad> &quads = mesh_cache[key];
    if (cell.MinTopFaces > 0)
    {
      const int top_count =
          CountTopFacesAt(quads, fluid_id, cell.Pos, chunk_coord);
      if (top_count < cell.MinTopFaces)
      {
        std::cerr << test_name << ": fluid cell missing top mesh face at ("
                  << cell.Pos.x << "," << cell.Pos.y << "," << cell.Pos.z
                  << ")" << std::endl;
        std::exit(1);
      }
    }
    if (cell.MinSideFacesToAir > 0)
    {
      const int side_count =
          CountVerticalSideFacesAt(quads, fluid_id, cell.Pos, chunk_coord);
      if (side_count < cell.MinSideFacesToAir)
      {
        std::cerr << test_name << ": fluid cell missing side mesh face at ("
                  << cell.Pos.x << "," << cell.Pos.y << "," << cell.Pos.z
                  << ")" << std::endl;
        std::exit(1);
      }
    }
  }
}

inline int CountVerticalFluidFaces(const std::vector<cutum::GreedyQuad> &quads,
                                 cutum::BlockId fluid_id)
{
  int count = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id == fluid_id && quad.axis != 1)
    {
      ++count;
    }
  }
  return count;
}

inline int CountShoreAirGaps(const cutum::UBlockWorld &world,
                             cutum::BlockId water_id, int sea_level,
                             int min_x, int max_x, int min_z, int max_z)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  int gaps = 0;
  for (int y = 1; y <= sea_level; ++y)
  {
    for (int x = min_x; x <= max_x; ++x)
    {
      for (int z = min_z; z <= max_z; ++z)
      {
        const glm::ivec3 pos(x, y, z);
        if (world.GetBlock(pos) != cutum::BLOCK_AIR)
        {
          continue;
        }
        for (const glm::ivec3 &offset : kDirs)
        {
          if (world.GetBlock(pos + offset) == water_id)
          {
            ++gaps;
            break;
          }
        }
      }
    }
  }
  return gaps;
}

inline bool IsFluidQueueCandidate(
    const cutum::UBlockWorld &world,
    const cutum::UBlockDefinitionStorage &definitions, glm::ivec3 pos)
{
  const cutum::BlockId id = world.GetBlock(pos);
  if (const cutum::BlockDefinition *def = definitions.GetById(id))
  {
    if (def->Physics.IsLiquid)
    {
      return true;
    }
  }
  return cutum::UFluidSpreadSystem::CanReceiveFluid(world, definitions, pos);
}

inline void TryEnqueueFluidAt(cutum::UFluidUpdateSet &queue,
                              const cutum::UBlockWorld &world,
                              const cutum::UBlockDefinitionStorage &definitions,
                              glm::ivec3 pos, uint64_t physics_tick)
{
  if (!IsFluidQueueCandidate(world, definitions, pos))
  {
    return;
  }
  if (!cutum::UFluidSpreadSystem::HasSpreadTargetForTick(world, definitions,
                                                         pos, physics_tick))
  {
    return;
  }
  queue.Enqueue(pos);
}

inline void WakeFluidAdjacency(cutum::UFluidUpdateSet &queue,
                               const cutum::UBlockWorld &world,
                               const cutum::UBlockDefinitionStorage &definitions,
                               glm::ivec3 center, uint64_t physics_tick)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    TryEnqueueFluidAt(queue, world, definitions, center + offset, physics_tick);
  }
}

inline void WakeFromFluidChange(
    cutum::UFluidUpdateSet &queue, const cutum::UBlockWorld &world,
    const cutum::UBlockDefinitionStorage &definitions,
    const cutum::FluidSpreadChange &change, uint64_t physics_tick)
{
  TryEnqueueFluidAt(queue, world, definitions, change.BlockPos, physics_tick);
  TryEnqueueFluidAt(queue, world, definitions, change.NeighborPos, physics_tick);
  WakeFluidAdjacency(queue, world, definitions, change.BlockPos, physics_tick);
  if (change.NeighborPos != change.BlockPos)
  {
    WakeFluidAdjacency(queue, world, definitions, change.NeighborPos,
                       physics_tick);
  }
}

inline void RunPhysicsFluidQueueTicks(
    cutum::UBlockWorld &world, const cutum::UBlockDefinitionStorage &definitions,
    cutum::UFluidUpdateSet &queue, cutum::UFluidSpreadSystem &fluid,
    uint64_t max_ticks)
{
  cutum::PhysicsBudgets budgets;
  budgets.FluidBlocksPerTickMax = 128;
  queue.SetBudgets(budgets);
  fluid.ShadowMode = false;

  for (uint64_t tick = 0; tick < max_ticks; ++tick)
  {
    const std::vector<glm::ivec3> batch = queue.PopBudgeted();
    if (batch.empty())
    {
      break;
    }
    for (glm::ivec3 pos : batch)
    {
      const cutum::FluidSpreadStats stats =
          fluid.TickBlock(world, definitions, tick, pos);
      for (const cutum::FluidSpreadChange &change : stats.Changes)
      {
        WakeFromFluidChange(queue, world, definitions, change, tick);
      }
    }
  }
}

inline void EnqueueFluidFrontier(cutum::UFluidUpdateSet &queue,
                                 cutum::UBlockWorld &world,
                                 const cutum::UBlockDefinitionStorage &definitions,
                                 glm::ivec3 block_pos)
{
  if (cutum::UFluidSpreadSystem::HasSpreadTarget(world, definitions, block_pos))
  {
    queue.Enqueue(block_pos);
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor = block_pos + offset;
    if (cutum::UFluidSpreadSystem::HasSpreadTarget(world, definitions, neighbor))
    {
      queue.Enqueue(neighbor);
    }
  }
}

inline void RunQueueTicks(cutum::UBlockWorld &world,
                          const cutum::UBlockDefinitionStorage &definitions,
                          cutum::UFluidUpdateSet &queue,
                          cutum::UFluidSpreadSystem &fluid, uint64_t max_ticks)
{
  cutum::PhysicsBudgets budgets;
  budgets.FluidBlocksPerTickMax = 128;
  queue.SetBudgets(budgets);
  fluid.ShadowMode = false;

  for (uint64_t tick = 0; tick < max_ticks; ++tick)
  {
    const std::vector<glm::ivec3> batch = queue.PopBudgeted();
    if (batch.empty())
    {
      break;
    }
    for (glm::ivec3 pos : batch)
    {
      const cutum::FluidSpreadStats stats =
          fluid.TickBlock(world, definitions, tick, pos);
      for (const cutum::FluidSpreadChange &change : stats.Changes)
      {
        WakeFromFluidChange(queue, world, definitions, change, tick);
      }
    }
  }
}

} // namespace FluidTest
