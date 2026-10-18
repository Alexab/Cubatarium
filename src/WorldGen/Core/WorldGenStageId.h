#pragma once

namespace cutum
{

enum class WorldGenStageId
{
  Terrain,
  Ravines,
  Valleys,
  Lakes,
  Caves,
  Fluids,
  Ores,
  Vegetation,
  GroundCover,
  Decoration,
  Structures,
  LavaPools,
  FirePatch,
  Geology,
  Aquifers,
  CaveFeatures,
};

constexpr int WorldGenStageIdCount = 16;

} // namespace cutum
