#include "World/Physics/FluidFloodService.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidPermeabilityUtil.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidTuning.h"

#include <algorithm>
#include <array>
#include <vector>

namespace cutum
{

namespace
{

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
  }
  return false;
}

bool IsFluidPermeableId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return IsFluidPermeableFromDefinition(id, definitions.GetById(id),
                                      IsLiquidId(definitions, id));
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid && def->Physics.FluidMaxLevel >= 7;
  }
  return false;
}

bool CellTouchesWetImpl(const UBlockWorld &blockWorld,
                        const UBlockDefinitionStorage &definitions,
                        glm::ivec3 pos)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor = pos + offset;
    const BlockId id = blockWorld.GetBlock(neighbor);
    if (IsLiquidId(definitions, id))
    {
      return true;
    }
    if (IsFluidPermeableId(definitions, id) &&
        PackFluidCellState(blockWorld.GetFluidState(neighbor)) != 0)
    {
      return true;
    }
  }
  return false;
}

BlockId ResolveFloodFluidIdImpl(const UBlockWorld &blockWorld,
                                const UBlockDefinitionStorage &definitions,
                                glm::ivec3 pos,
                                const FluidFloodOptions &options)
{
  if (options.fluid_id != BLOCK_AIR)
  {
    return options.fluid_id;
  }
  if (options.water_id != BLOCK_AIR && options.sea_level >= 0 &&
      pos.y <= options.sea_level)
  {
    return options.water_id;
  }
  if (options.water_id != BLOCK_AIR && options.sea_level >= 0 &&
      pos.y <= options.sea_level + FluidTuning::CoastalPermeableBandAboveSea)
  {
    const BlockId block_id = blockWorld.GetBlock(pos);
    if (IsFluidPermeableId(definitions, block_id))
    {
      return options.water_id;
    }
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  bool has_water = false;
  BlockId other_liquid = BLOCK_AIR;
  for (const glm::ivec3 &offset : kDirs)
  {
    const BlockId id = blockWorld.GetBlock(pos + offset);
    if (!IsLiquidId(definitions, id))
    {
      continue;
    }
    if (IsWaterKind(definitions, id))
    {
      has_water = true;
    }
    else if (other_liquid == BLOCK_AIR)
    {
      other_liquid = id;
    }
  }
  if (has_water && options.water_id != BLOCK_AIR)
  {
    return options.water_id;
  }
  return other_liquid;
}

bool IsWetCellImpl(const UBlockWorld &blockWorld,
                   const UBlockDefinitionStorage &definitions, glm::ivec3 pos)
{
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsLiquidId(definitions, id))
  {
    return true;
  }
  if (IsFluidPermeableId(definitions, id) &&
      PackFluidCellState(blockWorld.GetFluidState(pos)) != 0)
  {
    return true;
  }
  return false;
}

int FloodBreakSiteFromWetNeighborsImpl(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 break_pos, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  int filled = 0;
  const auto try_fill = [&](glm::ivec3 pos)
  {
    if (!UFluidSpreadSystem::CanReceiveFluid(blockWorld, definitions, pos))
    {
      return;
    }
    if (IsLiquidId(definitions, blockWorld.GetBlock(pos)))
    {
      return;
    }
    const BlockId fluid_id =
        ResolveFloodFluidIdImpl(blockWorld, definitions, pos, options);
    if (fluid_id == BLOCK_AIR)
    {
      return;
    }
    const FluidCellState state =
        options.source_for_air ? FluidCellState::Source()
                               : FluidCellState::Flowing(1);
    UFluidSpreadSystem::ApplyFluidFill(blockWorld, definitions, pos, fluid_id,
                                       state);
    ++filled;
    if (out_changed != nullptr)
    {
      out_changed->push_back(pos);
    }
    const glm::ivec3 above(pos.x, pos.y + 1, pos.z);
    if (IsFluidPermeableId(definitions, blockWorld.GetBlock(above)) &&
        PackFluidCellState(blockWorld.GetFluidState(above)) == 0)
    {
      UFluidSpreadSystem::ApplyFluidFill(blockWorld, definitions, above,
                                         fluid_id, FluidCellState::Flowing(1));
      ++filled;
      if (out_changed != nullptr)
      {
        out_changed->push_back(above);
      }
    }
  };

  if (CellTouchesWetImpl(blockWorld, definitions, break_pos))
  {
    try_fill(break_pos);
  }

  static constexpr std::array<glm::ivec3, 6> kWetNeighborDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  static constexpr std::array<glm::ivec3, 5> kSpillDirs = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};

  for (const glm::ivec3 &wet_offset : kWetNeighborDirs)
  {
    const glm::ivec3 wet_pos = break_pos + wet_offset;
    if (!IsWetCellImpl(blockWorld, definitions, wet_pos))
    {
      continue;
    }
    for (const glm::ivec3 &spill_offset : kSpillDirs)
    {
      try_fill(wet_pos + spill_offset);
    }
  }

  if (options.water_id != BLOCK_AIR && options.sea_level >= 0 &&
      break_pos.y <= options.sea_level + 1)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      for (int dy = 0; dy <= 6; ++dy)
      {
        for (int dz = -1; dz <= 1; ++dz)
        {
          const glm::ivec3 pos = break_pos + glm::ivec3(dx, dy, dz);
          if (pos.y <= options.sea_level ||
              pos.y > options.sea_level + FluidTuning::CoastalPermeableBandAboveSea)
          {
            continue;
          }
          if (!CellTouchesWetImpl(blockWorld, definitions, pos))
          {
            continue;
          }
          const BlockId block_id = blockWorld.GetBlock(pos);
          if (!IsFluidPermeableId(definitions, block_id))
          {
            continue;
          }
          if (PackFluidCellState(blockWorld.GetFluidState(pos)) != 0)
          {
            continue;
          }
          UFluidSpreadSystem::ApplyFluidFill(blockWorld, definitions, pos,
                                             options.water_id,
                                             FluidCellState::Flowing(1));
          ++filled;
          if (out_changed != nullptr)
          {
            out_changed->push_back(pos);
          }
        }
      }
    }
  }
  return filled;
}

int FloodWetPocketsInBoxImpl(UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 box_min, glm::ivec3 box_max,
                             const FluidFloodOptions &options,
                             std::vector<glm::ivec3> *out_changed)
{
  const glm::ivec3 min_corner(std::min(box_min.x, box_max.x),
                              std::min(box_min.y, box_max.y),
                              std::min(box_min.z, box_max.z));
  const glm::ivec3 max_corner(std::max(box_min.x, box_max.x),
                              std::max(box_min.y, box_max.y),
                              std::max(box_min.z, box_max.z));

  int filled = 0;
  bool changed = true;
  const int max_passes = std::max(1, options.max_passes);
  for (int pass = 0; changed && pass < max_passes; ++pass)
  {
    changed = false;
    std::vector<glm::ivec3> to_fill_air;
    std::vector<glm::ivec3> to_fill_permeable;
    to_fill_air.reserve(64);
    to_fill_permeable.reserve(64);
    for (int x = min_corner.x; x <= max_corner.x; ++x)
    {
      for (int y = min_corner.y; y <= max_corner.y; ++y)
      {
        for (int z = min_corner.z; z <= max_corner.z; ++z)
        {
          const glm::ivec3 pos(x, y, z);
          if (!CellTouchesWetImpl(blockWorld, definitions, pos))
          {
            continue;
          }
          const BlockId block_id = blockWorld.GetBlock(pos);
          if (block_id == BLOCK_AIR)
          {
            to_fill_air.push_back(pos);
            continue;
          }
          if (IsFluidPermeableId(definitions, block_id) &&
              PackFluidCellState(blockWorld.GetFluidState(pos)) == 0)
          {
            to_fill_permeable.push_back(pos);
          }
        }
      }
    }
    for (const glm::ivec3 &pos : to_fill_air)
    {
      const BlockId fluid_id =
          ResolveFloodFluidIdImpl(blockWorld, definitions, pos, options);
      if (fluid_id == BLOCK_AIR)
      {
        continue;
      }
      const FluidCellState state =
          options.source_for_air ? FluidCellState::Source()
                                 : FluidCellState::Flowing(1);
      UFluidSpreadSystem::ApplyFluidFill(blockWorld, definitions, pos, fluid_id,
                                         state);
      ++filled;
      changed = true;
      if (out_changed != nullptr)
      {
        out_changed->push_back(pos);
      }
    }
    for (const glm::ivec3 &pos : to_fill_permeable)
    {
      const BlockId fluid_id =
          ResolveFloodFluidIdImpl(blockWorld, definitions, pos, options);
      if (fluid_id == BLOCK_AIR)
      {
        continue;
      }
      UFluidSpreadSystem::ApplyFluidFill(blockWorld, definitions, pos, fluid_id,
                                         FluidCellState::Flowing(1));
      ++filled;
      changed = true;
      if (out_changed != nullptr)
      {
        out_changed->push_back(pos);
      }
    }
  }
  return filled;
}

} // namespace

bool UFluidFloodService::CellTouchesWet(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return CellTouchesWetImpl(blockWorld, definitions, pos);
}

BlockId UFluidFloodService::ResolveFloodFluidId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, const FluidFloodOptions &options)
{
  return ResolveFloodFluidIdImpl(blockWorld, definitions, pos, options);
}

int UFluidFloodService::FloodWetPocketsInBox(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 box_min, glm::ivec3 box_max, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return FloodWetPocketsInBoxImpl(blockWorld, definitions, box_min, box_max,
                                  options, out_changed);
}

int UFluidFloodService::FloodWetPocketsLocal(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 center, int radius, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  const int clamped_radius = std::max(0, radius);
  const glm::ivec3 box_min = center - glm::ivec3(clamped_radius);
  const glm::ivec3 box_max = center + glm::ivec3(clamped_radius);
  return FloodWetPocketsInBoxImpl(blockWorld, definitions, box_min, box_max,
                                  options, out_changed);
}

int UFluidFloodService::FloodBreakSiteFromWetNeighbors(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 break_pos, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return FloodBreakSiteFromWetNeighborsImpl(blockWorld, definitions, break_pos,
                                            options, out_changed);
}

} // namespace cutum
