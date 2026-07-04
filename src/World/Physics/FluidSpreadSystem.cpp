#include "World/Physics/FluidSpreadSystem.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/IUChunkMeshReader.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidPermeabilityUtil.h"
#include "World/Physics/FluidTuning.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <algorithm>
#include <array>
#include <vector>

namespace cutum
{

namespace
{

enum class NeighborAxis
{
  Upper,
  Lower,
  Horizontal,
};

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
  }
  return false;
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id);

BlockId ResolveWaterBlockIdImpl(const UBlockDefinitionStorage &definitions);

BlockId ResolveFluidBlockIdImpl(const UBlockWorld &blockWorld,
                                const UBlockDefinitionStorage &definitions,
                                glm::ivec3 block_pos);

BlockId ResolveFluidKindImpl(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId block_id);

bool IsFluidPermeableId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return IsFluidPermeableFromDefinition(id, definitions.GetById(id),
                                        IsLiquidId(definitions, id));
}

bool CellHasActiveFluid(const UBlockWorld &blockWorld,
                        const UBlockDefinitionStorage &definitions,
                        glm::ivec3 pos)
{
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsLiquidId(definitions, id))
  {
    return true;
  }
  if (IsFluidPermeableId(definitions, id))
  {
    return FluidCellHasActiveFluid(
        PackFluidCellState(blockWorld.GetFluidState(pos)));
  }
  return false;
}

bool ShouldReplaceBlockWithFluidImpl(const UBlockWorld &blockWorld,
                                     const UBlockDefinitionStorage &definitions,
                                     glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return false;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
}

FluidCellState StoredFluidStateForCell(const UBlockWorld &blockWorld,
                                       const UBlockDefinitionStorage &definitions,
                                       glm::ivec3 pos, FluidCellState state)
{
  if (ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos))
  {
    return state;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id) && state.IsSource())
  {
    FluidCellState flowing = FluidCellState::Flowing(1);
    if (state.HasExplicitKind())
    {
      flowing.SetKind(state.GetKind());
    }
    return flowing;
  }
  return state;
}

FluidKind FluidKindFromBlockIdImpl(const UBlockDefinitionStorage &definitions,
                                   BlockId id)
{
  if (IsWaterKind(definitions, id))
  {
    return FluidKind::Water;
  }
  if (IsLiquidId(definitions, id))
  {
    return FluidKind::Lava;
  }
  return FluidKind::None;
}

BlockId BlockIdFromFluidKindImpl(const UBlockDefinitionStorage &definitions,
                                 FluidKind kind)
{
  switch (kind)
  {
  case FluidKind::Water:
    return ResolveWaterBlockIdImpl(definitions);
  case FluidKind::Lava:
    for (const auto &entry : definitions.GetAll())
    {
      if (IsLiquidId(definitions, entry.first) &&
          !IsWaterKind(definitions, entry.first))
      {
        return entry.first;
      }
    }
    return BLOCK_AIR;
  default:
    return BLOCK_AIR;
  }
}

FluidCellState EnsureFluidKind(const UBlockDefinitionStorage &definitions,
                               BlockId fluid_id, FluidCellState state)
{
  if (state.HasExplicitKind())
  {
    return state;
  }
  const FluidKind kind = FluidKindFromBlockIdImpl(definitions, fluid_id);
  if (kind == FluidKind::None)
  {
    return state;
  }
  return state.WithKind(kind);
}

bool CanReceiveFluidImpl(const UBlockWorld &blockWorld,
                         const UBlockDefinitionStorage &definitions,
                         glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return true;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
}

bool IsLiquidRenewable(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.LiquidRenewable;
  }
  return false;
}

int GetSpreadPeriod(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, def->Physics.FluidSpreadPeriodTicks);
  }
  return 5;
}

int GetFluidMaxLevel(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, def->Physics.FluidMaxLevel);
  }
  return static_cast<int>(FLUID_LEVEL_MAX);
}

int GetLiquidViscosity(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, static_cast<int>(def->Physics.LiquidViscosity));
  }
  return 1;
}

int GetLiquidRange(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return std::min(static_cast<int>(FLUID_LEVEL_MAX) + 1,
                  GetFluidMaxLevel(definitions, id) + 1);
}

int MinSurviveLevel(const UBlockDefinitionStorage &definitions, BlockId id)
{
  const int max_level = GetFluidMaxLevel(definitions, id);
  const int min_level = static_cast<int>(FLUID_LEVEL_MAX) + 1 -
                        GetLiquidRange(definitions, id);
  if (min_level > max_level)
  {
    return 1;
  }
  return std::max(0, min_level);
}

FluidCellState EffectiveFluidState(const UBlockWorld &blockWorld,
                                   glm::ivec3 block_pos)
{
  FluidCellState state = blockWorld.GetFluidState(block_pos);
  if (state.Level == 0 && state.Falling == 0)
  {
    const uint8_t packed = PackFluidCellState(state);
    if (packed == 0)
    {
      return FluidCellState::Source();
    }
  }
  return state;
}

int LevelFromState(const FluidCellState &state)
{
  if (state.IsSource())
  {
    return 0;
  }
  return static_cast<int>(state.Level);
}

void RecordChange(FluidSpreadStats &stats, glm::ivec3 block_pos,
                  glm::ivec3 neighbor_pos, BlockId fluid_id,
                  FluidCellState new_state, bool removed_fluid,
                  const char *reason)
{
  ++stats.Applied;
  stats.Changes.push_back(
      {block_pos, neighbor_pos, fluid_id, new_state, removed_fluid});
  ULiquidDebugTrace::Instance().Record(block_pos, neighbor_pos, reason);
}

int MaxLevelFromNeighbor(const FluidCellState &neighbor_state,
                         NeighborAxis axis, int current_max)
{
  const int nb_level = LevelFromState(neighbor_state);
  switch (axis)
  {
  case NeighborAxis::Upper:
    if (neighbor_state.IsSource())
    {
      return std::max(current_max, 1);
    }
    if (nb_level > 0)
    {
      const int boosted = std::min(static_cast<int>(FLUID_LEVEL_MAX),
                                   nb_level + FluidTuning::WaterDropBoost);
      return std::max(current_max, boosted);
    }
    break;
  case NeighborAxis::Lower:
    break;
  case NeighborAxis::Horizontal:
    if (neighbor_state.Falling != 0)
    {
      break;
    }
    if (neighbor_state.IsSource())
    {
      return std::max(current_max, 1);
    }
    if (nb_level > 0)
    {
      return std::max(current_max, nb_level + 1);
    }
    break;
  }
  return current_max;
}

bool HorizontalSpreadAllowed(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos)
{
  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  return below.y < 0 || !CanReceiveFluidImpl(blockWorld, definitions, below);
}

BlockId ResolveWaterBlockIdImpl(const UBlockDefinitionStorage &definitions)
{
  for (const auto &entry : definitions.GetAll())
  {
    if (IsWaterKind(definitions, entry.first))
    {
      return entry.first;
    }
  }
  return BLOCK_AIR;
}

void ConsiderLiquidNeighbor(const UBlockDefinitionStorage &definitions,
                            BlockId id, BlockId &water_liquid,
                            BlockId &other_liquid)
{
  if (!IsLiquidId(definitions, id))
  {
    return;
  }
  if (IsWaterKind(definitions, id))
  {
    water_liquid = id;
    return;
  }
  if (other_liquid == BLOCK_AIR)
  {
    other_liquid = id;
  }
}

void ConsiderWaterloggedNeighbor(const UBlockWorld &blockWorld,
                                 const UBlockDefinitionStorage &definitions,
                                 glm::ivec3 neighbor_pos,
                                 BlockId &water_liquid, BlockId &other_liquid)
{
  const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
  if (!IsFluidPermeableId(definitions, neighbor_id) ||
      !CellHasActiveFluid(blockWorld, definitions, neighbor_pos))
  {
    return;
  }
  const FluidCellState neighbor_fluid = blockWorld.GetFluidState(neighbor_pos);
  if (neighbor_fluid.HasExplicitKind())
  {
    ConsiderLiquidNeighbor(
        definitions,
        BlockIdFromFluidKindImpl(definitions, neighbor_fluid.GetKind()),
        water_liquid, other_liquid);
    return;
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &inner_offset : kDirs)
  {
    ConsiderLiquidNeighbor(
        definitions, blockWorld.GetBlock(neighbor_pos + inner_offset),
        water_liquid, other_liquid);
  }
}

void ConsiderWaterloggedNeighborMesh(
    const IUChunkMeshReader &reader, const UBlockDefinitionStorage &definitions,
    glm::ivec3 neighbor_pos, BlockId &water_liquid, BlockId &other_liquid)
{
  const BlockId neighbor_id = reader.GetBlock(neighbor_pos);
  if (!IsFluidPermeableId(definitions, neighbor_id))
  {
    return;
  }
  const FluidCellState neighbor_fluid = reader.GetFluid(neighbor_pos);
  if (!FluidCellHasActiveFluid(PackFluidCellState(neighbor_fluid)))
  {
    return;
  }
  if (neighbor_fluid.HasExplicitKind())
  {
    ConsiderLiquidNeighbor(
        definitions,
        BlockIdFromFluidKindImpl(definitions, neighbor_fluid.GetKind()),
        water_liquid, other_liquid);
    return;
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &inner_offset : kDirs)
  {
    ConsiderLiquidNeighbor(definitions,
                           reader.GetBlock(neighbor_pos + inner_offset),
                           water_liquid, other_liquid);
  }
}

bool HasOutboundSpreadTarget(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId fluid_id)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    if (!CanReceiveFluidImpl(blockWorld, definitions, neighbor_pos))
    {
      continue;
    }
    const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
    if (IsLiquidId(definitions, neighbor_id))
    {
      if (ResolveFluidKindImpl(blockWorld, definitions, neighbor_pos,
                               neighbor_id) == fluid_id)
      {
        return true;
      }
      continue;
    }
    if (IsFluidPermeableId(definitions, neighbor_id) &&
        CellHasActiveFluid(blockWorld, definitions, neighbor_pos) &&
        ResolveFluidBlockIdImpl(blockWorld, definitions, neighbor_pos) ==
            fluid_id)
    {
      continue;
    }
    return true;
  }
  return false;
}

BlockId ResolveFluidKindImpl(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId block_id)
{
  if (IsLiquidId(definitions, block_id))
  {
    return block_id;
  }
  const FluidCellState fluid = blockWorld.GetFluidState(block_pos);
  if (fluid.HasExplicitKind())
  {
    return BlockIdFromFluidKindImpl(definitions, fluid.GetKind());
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  BlockId water_liquid = BLOCK_AIR;
  BlockId other_liquid = BLOCK_AIR;
  if (CellHasActiveFluid(blockWorld, definitions, block_pos))
  {
    for (const glm::ivec3 &offset : kDirs)
    {
      ConsiderLiquidNeighbor(definitions,
                             blockWorld.GetBlock(block_pos + offset),
                             water_liquid, other_liquid);
    }
  }
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    ConsiderLiquidNeighbor(definitions, blockWorld.GetBlock(neighbor_pos),
                           water_liquid, other_liquid);
    ConsiderWaterloggedNeighbor(blockWorld, definitions, neighbor_pos,
                                water_liquid, other_liquid);
  }
  if (water_liquid != BLOCK_AIR)
  {
    return water_liquid;
  }
  return other_liquid;
}

BlockId ResolveFluidKind(const UBlockWorld &blockWorld,
                         const UBlockDefinitionStorage &definitions,
                         glm::ivec3 block_pos, BlockId block_id)
{
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

bool NeighborProvidesFluid(const UBlockWorld &blockWorld,
                           const UBlockDefinitionStorage &definitions,
                           glm::ivec3 neighbor_pos, BlockId fluid_id)
{
  const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
  if (neighbor_id == fluid_id)
  {
    return true;
  }
  if (IsFluidPermeableId(definitions, neighbor_id) &&
      CellHasActiveFluid(blockWorld, definitions, neighbor_pos))
  {
    return ResolveFluidKindImpl(blockWorld, definitions, neighbor_pos,
                                neighbor_id) == fluid_id;
  }
  return false;
}

BlockId ResolveFluidBlockIdImpl(const UBlockWorld &blockWorld,
                                const UBlockDefinitionStorage &definitions,
                                glm::ivec3 block_pos)
{
  const BlockId block_id = blockWorld.GetBlock(block_pos);
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

void ApplyFluidFillImpl(UBlockWorld &blockWorld,
                        const UBlockDefinitionStorage &definitions,
                        glm::ivec3 pos, BlockId fluid_id, FluidCellState state)
{
  const FluidCellState with_kind =
      EnsureFluidKind(definitions, fluid_id, state);
  const FluidCellState stored =
      StoredFluidStateForCell(blockWorld, definitions, pos, with_kind);
  if (ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos))
  {
    blockWorld.SetBlock(pos, fluid_id);
  }
  blockWorld.SetFluidState(pos, stored);
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
    if (!CanReceiveFluidImpl(blockWorld, definitions, pos))
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
    ApplyFluidFillImpl(blockWorld, definitions, pos, fluid_id, state);
    ++filled;
    if (out_changed != nullptr)
    {
      out_changed->push_back(pos);
    }
    const glm::ivec3 above(pos.x, pos.y + 1, pos.z);
    if (IsFluidPermeableId(definitions, blockWorld.GetBlock(above)) &&
        PackFluidCellState(blockWorld.GetFluidState(above)) == 0)
    {
      ApplyFluidFillImpl(blockWorld, definitions, above, fluid_id,
                     FluidCellState::Flowing(1));
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
          ApplyFluidFillImpl(blockWorld, definitions, pos, options.water_id,
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
      ApplyFluidFillImpl(blockWorld, definitions, pos, fluid_id, state);
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
      ApplyFluidFillImpl(blockWorld, definitions, pos, fluid_id,
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

bool UFluidSpreadSystem::CellTouchesWet(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return CellTouchesWetImpl(blockWorld, definitions, pos);
}

BlockId UFluidSpreadSystem::ResolveFloodFluidId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, const FluidFloodOptions &options)
{
  return ResolveFloodFluidIdImpl(blockWorld, definitions, pos, options);
}

int UFluidSpreadSystem::FloodWetPocketsInBox(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 box_min, glm::ivec3 box_max, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return FloodWetPocketsInBoxImpl(blockWorld, definitions, box_min, box_max,
                                  options, out_changed);
}

int UFluidSpreadSystem::FloodWetPocketsLocal(
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

int UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 break_pos, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return FloodBreakSiteFromWetNeighborsImpl(blockWorld, definitions, break_pos,
                                              options, out_changed);
}

bool UFluidSpreadSystem::CanReceiveFluid(const UBlockWorld &blockWorld,
                                         const UBlockRegistry &registry,
                                         glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return blockWorld.IsAir(pos);
  }
  return CanReceiveFluidImpl(blockWorld, *definitions, pos);
}

bool UFluidSpreadSystem::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockRegistry &registry,
    glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return blockWorld.IsAir(pos);
  }
  return ShouldReplaceBlockWithFluidImpl(blockWorld, *definitions, pos);
}

bool UFluidSpreadSystem::CanReceiveFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return CanReceiveFluidImpl(blockWorld, definitions, pos);
}

bool UFluidSpreadSystem::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos);
}

bool UFluidSpreadSystem::ShouldProcessFluidTick(uint64_t physics_tick,
                                                glm::ivec3 block_pos,
                                                int spread_period)
{
  const int period = std::max(1, spread_period);
  const uint32_t x = static_cast<uint32_t>(block_pos.x);
  const uint32_t y = static_cast<uint32_t>(block_pos.y);
  const uint32_t z = static_cast<uint32_t>(block_pos.z);
  const uint32_t phase = (x * 73856093u ^ y * 19349663u ^ z * 83492791u) %
                         static_cast<uint32_t>(period);
  return (physics_tick + phase) % static_cast<uint32_t>(period) == 0;
}

bool UFluidSpreadSystem::HasSpreadTarget(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  const BlockId block_id = blockWorld.GetBlock(block_pos);
  if (IsLiquidId(definitions, block_id))
  {
    const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
    if (below.y >= 0 && CanReceiveFluidImpl(blockWorld, definitions, below))
    {
      return true;
    }
    if (!HorizontalSpreadAllowed(blockWorld, definitions, block_pos))
    {
      return true;
    }
    static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
        glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
        glm::ivec3(0, 0, 1)};
    const FluidCellState self_state =
        EffectiveFluidState(blockWorld, block_pos);
    const int self_level = LevelFromState(self_state);
    const int max_level = GetFluidMaxLevel(definitions, block_id);
    if (self_state.IsSource() || self_level < max_level)
    {
      for (const glm::ivec3 &offset : kSideOffsets)
      {
        const glm::ivec3 side = block_pos + offset;
        const glm::ivec3 side_below(side.x, side.y - 1, side.z);
        if (side_below.y >= 0 &&
            CanReceiveFluidImpl(blockWorld, definitions, side_below))
        {
          continue;
        }
        if (CanReceiveFluidImpl(blockWorld, definitions, side))
        {
          return true;
        }
        if (NeighborProvidesFluid(blockWorld, definitions, side, block_id))
        {
          const FluidCellState neighbor_state =
              EffectiveFluidState(blockWorld, side);
          if (LevelFromState(neighbor_state) + 1 < self_level)
          {
            return true;
          }
        }
      }
    }
    return self_state.IsSource();
  }

  if (!CanReceiveFluidImpl(blockWorld, definitions, block_pos))
  {
    return false;
  }
  const BlockId fluid_id =
      ResolveFluidKind(blockWorld, definitions, block_pos, block_id);
  if (fluid_id == BLOCK_AIR)
  {
    return false;
  }
  if (IsFluidPermeableId(definitions, block_id) &&
      CellHasActiveFluid(blockWorld, definitions, block_pos))
  {
    const FluidCellState self_fluid = blockWorld.GetFluidState(block_pos);
    if (self_fluid.HasExplicitKind())
    {
      return HasOutboundSpreadTarget(blockWorld, definitions, block_pos,
                                     fluid_id);
    }
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    if (!NeighborProvidesFluid(blockWorld, definitions, neighbor_pos, fluid_id))
    {
      continue;
    }
    const FluidCellState neighbor_state =
        EffectiveFluidState(blockWorld, neighbor_pos);
    if (neighbor_state.IsSource())
    {
      return true;
    }
    if (LevelFromState(neighbor_state) + 1 <=
        GetFluidMaxLevel(definitions, fluid_id))
    {
      return true;
    }
  }
  return false;
}

FluidSpreadStats UFluidSpreadSystem::Tick(UWorld &world, glm::ivec3 block_pos)
{
  const UBlockDefinitionStorage *definitions =
      world.GetBlockRegistry().GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(world.GetBlockWorld(), *definitions,
                   world.GetPhysicsTickCounter(), block_pos,
                   world.GetProceduralSettings().FillWater
                       ? world.GetProceduralSettings().SeaLevel
                       : -1);
}

FluidSpreadStats UFluidSpreadSystem::TickBlock(UBlockWorld &blockWorld,
                                               const UBlockRegistry &registry,
                                               uint64_t physics_tick,
                                               glm::ivec3 block_pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(blockWorld, *definitions, physics_tick, block_pos, -1);
}

FluidSpreadStats
UFluidSpreadSystem::TickBlock(UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              uint64_t physics_tick, glm::ivec3 block_pos,
                              int sea_level)
{
  FluidSpreadStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    return stats;
  }

  const BlockId block_id = blockWorld.GetBlock(block_pos);
  const bool is_liquid = IsLiquidId(definitions, block_id);
  const bool is_waterlogged_permeable =
      !is_liquid && IsFluidPermeableId(definitions, block_id) &&
      CellHasActiveFluid(blockWorld, definitions, block_pos);
  const bool is_floodable =
      CanReceiveFluidImpl(blockWorld, definitions, block_pos);
  if (!is_liquid && !is_floodable)
  {
    return stats;
  }

  BlockId fluid_id =
      ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
  const FluidCellState cell_fluid = blockWorld.GetFluidState(block_pos);
  if (fluid_id == BLOCK_AIR && is_waterlogged_permeable && sea_level >= 0 &&
      block_pos.y <= sea_level + FluidTuning::CoastalPermeableBandAboveSea && !cell_fluid.HasExplicitKind())
  {
    fluid_id = ResolveWaterBlockIdImpl(definitions);
  }
  if (sea_level >= 0 && block_pos.y <= sea_level && is_liquid &&
      !IsWaterKind(definitions, fluid_id))
  {
    return stats;
  }
  if (sea_level >= 0 && is_waterlogged_permeable &&
      block_pos.y <= sea_level + FluidTuning::CoastalPermeableBandAboveSea && !cell_fluid.HasExplicitKind())
  {
    const BlockId water_id = ResolveWaterBlockIdImpl(definitions);
    if (water_id != BLOCK_AIR)
    {
      fluid_id = water_id;
    }
  }
  if (fluid_id == BLOCK_AIR)
  {
    return stats;
  }

  const int spread_period = GetSpreadPeriod(definitions, fluid_id);
  const int max_level = GetFluidMaxLevel(definitions, fluid_id);
  const int min_survive = MinSurviveLevel(definitions, fluid_id);
  const int viscosity = GetLiquidViscosity(definitions, fluid_id);
  const bool renewable = IsLiquidRenewable(definitions, fluid_id);

  FluidCellState current_state =
      (is_liquid || is_waterlogged_permeable)
          ? EffectiveFluidState(blockWorld, block_pos)
          : FluidCellState::Flowing(
                static_cast<uint8_t>(std::min(max_level,
                                              static_cast<int>(FLUID_LEVEL_MAX))));
  const int current_level =
      (is_liquid || is_waterlogged_permeable)
          ? LevelFromState(current_state)
          : max_level + 1;

  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  const bool below_floodable =
      below.y >= 0 && CanReceiveFluidImpl(blockWorld, definitions, below);
  const bool flowing_down = below_floodable;

  if (is_liquid && below_floodable &&
      ShouldProcessFluidTick(physics_tick, block_pos, spread_period))
  {
    const FluidCellState below_state = FluidCellState::Flowing(1, true);
    ApplyFluidFillImpl(blockWorld, definitions, below, fluid_id, below_state);
    RecordChange(stats, block_pos, below, fluid_id, below_state, false,
                 "spread_down");
    return stats;
  }

  if (is_liquid &&
      !ShouldProcessFluidTick(physics_tick, block_pos, spread_period))
  {
    return stats;
  }

  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  static constexpr std::array<NeighborAxis, 6> kAxes = {
      NeighborAxis::Upper,      NeighborAxis::Horizontal,
      NeighborAxis::Horizontal, NeighborAxis::Horizontal,
      NeighborAxis::Horizontal, NeighborAxis::Lower};

  int source_count = 0;
  bool source_not_below = false;
  int max_incoming_level = 0;

  for (size_t i = 0; i < kDirs.size(); ++i)
  {
    const glm::ivec3 neighbor_pos = block_pos + kDirs[i];
    if (!NeighborProvidesFluid(blockWorld, definitions, neighbor_pos, fluid_id))
    {
      continue;
    }
    const FluidCellState neighbor_state =
        EffectiveFluidState(blockWorld, neighbor_pos);
    if (neighbor_state.IsSource())
    {
      if (kAxes[i] != NeighborAxis::Lower)
      {
        ++source_count;
        source_not_below = true;
      }
      continue;
    }
    if (is_liquid && kAxes[i] == NeighborAxis::Horizontal &&
        !HorizontalSpreadAllowed(blockWorld, definitions, block_pos))
    {
      continue;
    }
    const glm::ivec3 side_below(neighbor_pos.x, neighbor_pos.y - 1,
                                neighbor_pos.z);
    if (is_liquid && kAxes[i] == NeighborAxis::Horizontal &&
        side_below.y >= 0 &&
        CanReceiveFluidImpl(blockWorld, definitions, side_below))
    {
      continue;
    }
    max_incoming_level =
        MaxLevelFromNeighbor(neighbor_state, kAxes[i], max_incoming_level);
  }

  bool new_is_source = false;
  int new_level = 0;

  if ((source_count >= 2 && renewable) ||
      (is_liquid && current_state.IsSource()))
  {
    new_is_source = true;
  }
  else if (source_count >= 1 && source_not_below)
  {
    new_level = 1;
  }
  else
  {
    new_level = max_incoming_level;
    if (is_liquid && viscosity > 1 && new_level != current_level &&
        new_level > 0)
    {
      const int level_inc = new_level - current_level;
      if (level_inc < -viscosity || level_inc > viscosity)
      {
        new_level = current_level + level_inc / viscosity;
      }
      else if (level_inc < 0)
      {
        new_level = current_level - 1;
      }
      else if (level_inc > 0)
      {
        new_level = current_level + 1;
      }
    }
  }

  if (new_is_source)
  {
    if (!is_liquid || !current_state.IsSource())
    {
      ApplyFluidFillImpl(blockWorld, definitions, block_pos, fluid_id,
                   FluidCellState::Source());
      RecordChange(stats, block_pos, block_pos, fluid_id,
                   FluidCellState::Source(), false, "transform_source");
    }
    return stats;
  }

  if (new_level <= 0 || new_level < min_survive || new_level > max_level)
  {
    if (is_liquid)
    {
      blockWorld.SetBlock(block_pos, BLOCK_AIR);
      blockWorld.ClearFluidState(block_pos);
      RecordChange(stats, block_pos, block_pos, fluid_id, FluidCellState{},
                   true, "transform_dry");
    }
    else if (is_waterlogged_permeable)
    {
      const int current_wet = LevelFromState(current_state);
      if (current_state.HasExplicitKind() && current_wet > 0 &&
          HasOutboundSpreadTarget(blockWorld, definitions, block_pos, fluid_id))
      {
        return stats;
      }
      blockWorld.ClearFluidState(block_pos);
      RecordChange(stats, block_pos, block_pos, fluid_id, FluidCellState{},
                   true, "transform_dry");
    }
    return stats;
  }

  const FluidCellState new_state =
      EnsureFluidKind(definitions, fluid_id,
                      FluidCellState::Flowing(static_cast<uint8_t>(new_level),
                                              flowing_down));
  const bool unchanged = (is_liquid || is_waterlogged_permeable) &&
                         !current_state.IsSource() &&
                         current_state.Level == new_state.Level &&
                         current_state.Falling == new_state.Falling &&
                         current_state.GetKind() == new_state.GetKind();
  if (unchanged)
  {
    return stats;
  }

  ApplyFluidFillImpl(blockWorld, definitions, block_pos, fluid_id, new_state);
  RecordChange(stats, block_pos, block_pos, fluid_id, new_state, false,
               is_liquid ? "transform_flow" : "transform_flood");
  return stats;
}

BlockId UFluidSpreadSystem::ResolveWaterBlockId(
    const UBlockDefinitionStorage &definitions)
{
  return ResolveWaterBlockIdImpl(definitions);
}

BlockId UFluidSpreadSystem::ResolveFluidKind(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos, BlockId block_id)
{
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

FluidKind UFluidSpreadSystem::FluidKindFromBlockId(
    const UBlockDefinitionStorage &definitions, BlockId id)
{
  return FluidKindFromBlockIdImpl(definitions, id);
}

BlockId UFluidSpreadSystem::BlockIdFromFluidKind(
    const UBlockDefinitionStorage &definitions, FluidKind kind)
{
  return BlockIdFromFluidKindImpl(definitions, kind);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  return ResolveFluidBlockIdImpl(blockWorld, definitions, block_pos);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockIdForMesh(
    const IUChunkMeshReader &reader,
    const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos)
{
  const BlockId block_id = reader.GetBlock(block_pos);
  if (IsLiquidId(definitions, block_id))
  {
    return block_id;
  }
  const FluidCellState fluid = reader.GetFluid(block_pos);
  if (fluid.HasExplicitKind())
  {
    return BlockIdFromFluidKindImpl(definitions, fluid.GetKind());
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  BlockId water_liquid = BLOCK_AIR;
  BlockId other_liquid = BLOCK_AIR;
  if (FluidCellHasActiveFluid(PackFluidCellState(fluid)))
  {
    for (const glm::ivec3 &offset : kDirs)
    {
      ConsiderLiquidNeighbor(definitions,
                             reader.GetBlock(block_pos + offset), water_liquid,
                             other_liquid);
    }
  }
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    ConsiderLiquidNeighbor(definitions, reader.GetBlock(neighbor_pos),
                           water_liquid, other_liquid);
    ConsiderWaterloggedNeighborMesh(reader, definitions, neighbor_pos,
                                    water_liquid, other_liquid);
  }
  if (water_liquid != BLOCK_AIR)
  {
    return water_liquid;
  }
  return other_liquid;
}

void UFluidSpreadSystem::ApplyFluidFill(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, BlockId fluid_id, FluidCellState state)
{
  ApplyFluidFillImpl(blockWorld, definitions, pos, fluid_id, state);
}

} // namespace cutum
