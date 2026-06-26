#pragma once

namespace cutum
{

enum class WorldGenStageId
{
  Terrain,
  Ravines,
  Caves,
  Fluids,
  Ores,
  Vegetation,
  GroundCover,
  Decoration,
  Structures,
  LavaPools,
  FirePatch,
};

constexpr int WorldGenStageIdCount = 11;

} // namespace cutum
