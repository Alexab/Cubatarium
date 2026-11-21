#pragma once
#include "WorldGen/Core/ProceduralSettings.h"
#include <cstring>
#include <tuple>
#include <utility>

namespace cutum
{
// Fixed inline input key: compare values, never a lossy hash or retained text.
// Seed/preset labels are metadata; the captured Pack owns pack selection.
inline auto MakePipelineSettingsKey(const ProceduralSettings &s)
{
  const auto bits=[](float value) {
    static_assert(sizeof(float)==sizeof(uint32_t));
    uint32_t result;std::memcpy(&result,&value,sizeof(result));return result;
  };
  const auto &t=s.Tuning;const auto &c=s.Caves;const auto &r=s.Ravines;
  return std::make_tuple(
      s.Generator,s.Seed,s.SeedKind,s.SeedHashAlgo,s.SeaLevel,s.MinHeight,s.MaxHeight,s.BedrockTopY,
      s.WorldGenProfileVersion,s.EnableCaves,s.EnableTrees,s.EnableGroundCover,s.EnableDecoration,
      s.EnableStructures,s.EnableOres,s.AsyncChunkGeneration,s.AsyncChunkIo,s.AsyncRelight,
      s.RelightThreadCount,s.RingGateEnabled,s.MaxChunkCommitsPerFrame,s.MaxLoadOpsPerFrame,
      s.MaxUnloadOpsPerFrame,s.MaxChunkCommitsPerFrameBoost,s.MaxLoadOpsPerFrameBoost,
      bits(s.MovementSpeedBoostThreshold),bits(s.MovementPrefetchThreshold),s.FlatSurfaceY,
      s.FillWater,s.FillLava,s.FillFire,s.DebugWorldGenOverlay,s.TerrainBackendMode,
      std::make_tuple(bits(t.vegetationDensity),bits(t.decorationDensity),bits(t.structureDensity),
          bits(t.biomePlainsWeight),bits(t.biomeForestWeight),bits(t.biomeDesertWeight),
          bits(t.biomeHillsWeight),bits(t.biomeTundraWeight),bits(t.biomeSavannaWeight),
          bits(t.biomeFoothillsWeight),bits(t.biomeScrublandWeight),bits(t.biomeColdSteppeWeight),
          bits(t.terrainRoughness),bits(t.biomeBlendRadius),bits(t.oreDensity),bits(t.terrainErosion),
          bits(t.riverWidth),bits(t.erosionStrength),t.heightSmoothing,t.heightSmoothingRadius,
          bits(t.jitterAmplitude),t.useUnifiedHeightField,t.useAnalyticValleys,t.useMudflowErosion,
          t.useDensityRefineParity),
      std::make_tuple(bits(c.threshold),c.minY,c.maxDepthBelowSurface,c.minDepthBelowSurface,c.octaves,
          bits(c.persistence),bits(c.lacunarity),bits(c.scale),c.style,c.useDensityField,
          bits(c.densityCaveAmplitude),bits(c.chunkGateThreshold),c.enableChunkGate,bits(c.cheeseScale),
          bits(c.cheeseWeight),bits(c.spaghettiScale),bits(c.spaghettiWeight),bits(c.noodleScale),bits(c.noodleWeight)),
      std::make_tuple(r.enabled,r.rarity,r.minDepth,r.maxDepth,r.aquaticMaxDepth,r.fillWater,r.featherMode,r.gentleRadius));
}
using PipelineSettingsKey=decltype(MakePipelineSettingsKey(std::declval<const ProceduralSettings &>()));
} // namespace cutum
