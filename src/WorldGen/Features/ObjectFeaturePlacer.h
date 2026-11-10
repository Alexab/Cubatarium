#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

bool CanPlaceObjectAt(const WorldGenContext &ctx, const std::string &prefabName,
                      glm::ivec3 anchorWorldPos);
bool PlaceObjectAt(WorldGenContext &ctx, const std::string &prefabName,
                   glm::ivec3 anchorWorldPos, int surfaceY = -1);

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome,
                                const BiomeKey &biomeKey = {});
bool TryPlaceGroundCoverFeatures(WorldGenContext &ctx, int x, int z, int surfaceY,
                                 BiomeId biome, bool skipIfTreeNearby,
                                 const BiomeKey &biomeKey = {});
bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome,
                                const BiomeKey &biomeKey = {});
bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome,
                               const BiomeKey &biomeKey = {});
bool TryPlaceCaveFeatures(
    WorldGenContext &ctx, int x, int z, int surfaceY,
    const std::function<BiomeId(int, int, int, int)> &biome_at_3d,
    const std::function<BiomeKey(int, int, int, int)> &biome_key_at_3d = {});

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome);
bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId);

struct ObjectFeatureRuleProfile
{
  std::string pool;
  size_t ruleIndex{0};
  std::string featureKey;
  uint64_t evaluated{0};
  uint64_t candidates{0};
  uint64_t placementAttempts{0};
  uint64_t placed{0};
  double evaluationMs{0.0};
  double placementMs{0.0};
  uint64_t prefabVoxelVisits{0};
  uint64_t prefabSurfaceScans{0};
};

// Per-worker accumulation is flushed once per chunk. Enabled only by the
// existing worldgen audit switch; placement behavior is unchanged.
struct ObjectFeatureChunkDiagnosticsScope
{
  explicit ObjectFeatureChunkDiagnosticsScope(bool enabled);
  ~ObjectFeatureChunkDiagnosticsScope();
};

struct ObjectFeatureDiagnostics
{
  static std::vector<ObjectFeatureRuleProfile> GetSummary();
};

void RecordObjectFeaturePlacementCheck(const std::string &prefabName,
                                       uint64_t voxelVisits,
                                       uint64_t surfaceScans);

void ResetScatterChunkCounts();
/// Ensure built-in placement strategies are available for direct registry use.
void RegisterBuiltinObjectFeaturePlacementStrategies();

} // namespace cutum
