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
bool TryPlaceCaveFeaturesRef(
    WorldGenContext &ctx,int x,int z,int surfaceY,
    const std::function<BiomeId(int,int,int,int)> &biome_at_3d,
    const std::function<BiomeKeyRef(int,int,int,int)> &biome_key_at_3d);

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome);
bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId);

struct ObjectFeatureRuleProfile : ObjectFeatureRuleCounters
{
  std::string pool;
  size_t ruleIndex{0};
  std::string featureKey;
};

// Per-worker accumulation is flushed once per chunk. Enabled only by the
// existing worldgen audit switch; placement behavior is unchanged.
// Scatter count storage is retired at chunk boundaries even when disabled.
struct ObjectFeatureChunkDiagnosticsScope
{
  explicit ObjectFeatureChunkDiagnosticsScope(bool enabled);
  ~ObjectFeatureChunkDiagnosticsScope() noexcept;
  ObjectFeatureChunkDiagnosticsScope(const ObjectFeatureChunkDiagnosticsScope &)=delete;
  ObjectFeatureChunkDiagnosticsScope &operator=(const ObjectFeatureChunkDiagnosticsScope &)=delete;
};

struct ObjectFeatureDiagnosticSnapshot
{
  uint64_t catalogRevision{0};
  size_t catalogRuleCount{0},catalogNumericCapacityBytes{0},missingWarningCount{0};
  std::vector<ObjectFeatureRuleProfile> profiles;
};

struct ObjectFeatureDiagnostics
{
  static std::vector<ObjectFeatureRuleProfile> GetSummary();
  static ObjectFeatureDiagnosticSnapshot GetSnapshot(const ObjectFeatureConfig *catalog=nullptr);
  static size_t GetThreadScratchBytes();
};

void RecordObjectFeaturePlacementCheck(const std::string &prefabName,
                                       uint64_t voxelVisits,
                                       uint64_t surfaceScans);

void ResetScatterChunkCounts();
/// Ensure built-in placement strategies are available for direct registry use.
void RegisterBuiltinObjectFeaturePlacementStrategies();

// Uses the same rules and strategy registry as placement, with the snapshot
// and object library held stable by the caller for the inspected operation.
std::optional<WorldGenWriteExtent>
GetObjectFeatureWriteExtent(const WorldGenContext &ctx, ObjectFeaturePool pool);
bool CaveFeaturesWriteWithinTerrainColumn(const WorldGenContext &ctx);
WorldGenWriteExtent GetPrefabWriteExtent(const WorldGenContext &ctx,
                                        const std::string &name,
                                        int anchor_search_radius = 0);

} // namespace cutum
