#pragma once

#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenStageId.h"
#include <array>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UObjectLibrary;
class IUWorldGenPipeline;
struct ObjectFeatureConfig;

struct ChunkPopulateRequest
{
  glm::ivec3 chunkCoord;
  ChunkGenerationToken token;
  ProceduralSettings settings;
  glm::ivec2 columnOrigin{0};
  bool hasColumnOrigin{false};
  UObjectLibrary *objects{nullptr};
  /// Captured on schedule thread; pinned for the whole Populate job.
  WorldGenContentSnapshot content;
  std::function<bool()> shouldCancel;
};

struct ChunkPopulateTiming
{
  double totalMs{0.0};
  double sampleMs{0.0};
  double terrainMs{0.0};
  double carveMs{0.0};
  double postMs{0.0};
  double sealMs{0.0};
  double mudflowMs{0.0};
  double pocketSealMs{0.0};
  double permeableSealMs{0.0};
  double permeableFloorScanMs{0.0};
  double permeableRestoreMs{0.0};
  double permeableWaterlogMs{0.0};
  double pocketResealMs{0.0};
  double vegetationPruneMs{0.0};
  std::array<double, WorldGenStageIdCount> postStageMs{};
  uint64_t mudflowColumns{0};
  uint64_t mudflowVoxelsMoved{0};
  uint64_t pocketVoxelsFilled{0};
  uint64_t permeableColumns{0};
  uint64_t permeableColumnsChanged{0};
  uint64_t permeableFloorGroundChecks{0};
  uint64_t permeableFloorSlotChecks{0};
  uint64_t permeableRestoreCells{0};
  uint64_t permeableWaterlogCells{0};
  uint64_t resealVoxelsFilled{0};
  uint64_t pruneColumns{0};
  uint64_t prunedVoxels{0};
  bool detailEnabled{false};
};

struct ChunkPopulateMetric
{
  uint64_t count{0};
  double totalMs{0.0};
  double medianMs{0.0};
  double p95Ms{0.0};
  double maxMs{0.0};
};

struct ChunkPopulateSummary
{
  uint64_t completedCount{0};
  uint64_t discardedCount{0};
  uint64_t detailCount{0};
  ChunkPopulateMetric total;
  ChunkPopulateMetric sample;
  ChunkPopulateMetric terrain;
  ChunkPopulateMetric carve;
  ChunkPopulateMetric post;
  ChunkPopulateMetric seal;
  ChunkPopulateMetric mudflow;
  ChunkPopulateMetric pocketSeal;
  ChunkPopulateMetric permeableSeal;
  ChunkPopulateMetric permeableFloorScan;
  ChunkPopulateMetric permeableRestore;
  ChunkPopulateMetric permeableWaterlog;
  ChunkPopulateMetric pocketReseal;
  ChunkPopulateMetric vegetationPrune;
  std::array<ChunkPopulateMetric, WorldGenStageIdCount> postStages{};
  uint64_t mudflowColumns{0};
  uint64_t mudflowVoxelsMoved{0};
  uint64_t pocketVoxelsFilled{0};
  uint64_t permeableColumns{0};
  uint64_t permeableColumnsChanged{0};
  uint64_t permeableFloorGroundChecks{0};
  uint64_t permeableFloorSlotChecks{0};
  uint64_t permeableRestoreCells{0};
  uint64_t permeableWaterlogCells{0};
  uint64_t resealVoxelsFilled{0};
  uint64_t pruneColumns{0};
  uint64_t prunedVoxels{0};
};

struct ChunkPopulateDiagnostics
{
  static void Record(const ChunkPopulateTiming &timing, bool discarded = false);
  static ChunkPopulateTiming GetLast();
  static ChunkPopulateSummary GetSummary();
};

struct ChunkPopulateResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  UChunkBuffer buffer;
  /// True when populate aborted mid-chunk (cancel); must not ApplyTo/seal.
  bool discarded{false};
  /// IntraChunkSeal already applied in gen buffer (V_fluid: skip pocket seal on
  /// commit). LiveShoreAir still deferred via DeferredShoreSealQueue.
  bool fluidSealed{false};
};

class IUChunkPopulator
{
public:
  virtual ~IUChunkPopulator() = default;
  virtual ChunkPopulateResult Populate(const ChunkPopulateRequest &request) = 0;
};

class UPipelineChunkPopulator : public IUChunkPopulator
{
public:
  UPipelineChunkPopulator(UBlockRegistry &registry, UObjectLibrary *prefabs,
                          std::string worldgenOwnerPackId);

  ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override;

private:
  UBlockRegistry &Registry;
  UObjectLibrary *Objects;
  std::string WorldgenOwnerPackId;
};

/// Deterministic cross-chunk priorities for every worldgen feature voxel:
/// procedural trees/shrubs/scatter plus object voxels from the vegetation,
/// ground cover, decoration and structure pools. Blocks absent here fall
/// back to rank 0 (terrain rules) in cross-chunk conflict resolution.
std::vector<std::pair<BlockId, int>> CollectFloraFeatureBlockRanks(
    UBlockRegistry &registry, UObjectLibrary *objects,
    const std::string &ownerPackId, const ObjectFeatureConfig *features);

} // namespace cutum
