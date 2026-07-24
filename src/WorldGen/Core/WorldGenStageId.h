#pragma once

namespace cutum
{

enum class WorldGenStageId
{
  Terrain,
  Ravines,
  Valleys,
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

constexpr int WorldGenStageIdCount = 12;

} // namespace cutum
