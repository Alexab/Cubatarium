#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H
#include "App/Settings/RenderSettings.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/ChunkDirtySet.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace cutum
{
struct Frustum;
struct MeshBuildResult;

struct MeshRebuildTickStats
{
  int Completed{0};
  int Scheduled{0};
  int SyncRebuilt{0};
};

struct FaceInstance
{
  glm::mat4 model{1.0f};
  BlockId Id{BLOCK_AIR};
  int faceIndex{0};
  glm::vec2 quadSize{1.0f, 1.0f};
};
using BlockInstance = FaceInstance;
class UBlockRegistry;
class UBlockWorld;
class UChunkMeshCache
{
public:
  void MarkAllDirty();
  void MarkAllDirtyFromWorld(const UBlockWorld &world,
                             bool clear_existing_caches = false);
  void MarkDirty(glm::ivec3 chunkCoord);
  void MarkDirtyPriority(glm::ivec3 chunkCoord);
  void RemoveChunk(glm::ivec3 chunkCoord);
  void RebuildDirtyChunks(UBlockWorld &world, UBlockRegistry &registry,
                          int max_drain_per_frame = 8,
                          int max_schedule_per_frame = 8);
  MeshRebuildTickStats RebuildDirtyChunksWithStats(
      UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
      int max_schedule_per_frame, bool force_sync = false);
  void RebuildAll(UBlockWorld &world, UBlockRegistry &registry);
  void RebuildChunkImmediate(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 chunkCoord);
  bool HasPendingDirty() const;
  bool HasDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                      int radius_chunks) const;
  bool HasPendingAsyncMeshWork() const;
  void WaitForAsyncMeshIdle();
  bool WaitForAsyncMeshIdleFor(std::chrono::milliseconds timeout);
  void CancelAsyncMeshWork();
  void CancelAsyncInFlightKeepDirty();
  void DrainAsyncMeshResults(UBlockWorld &world, UBlockRegistry &registry,
                             int max_per_frame);
  double GetLastFlatRebuildMs() const { return LastFlatRebuildMs; }
  int GetAsyncInFlightCount() const;
  size_t GetGreedyCacheSize() const { return GreedyCache.size(); }
  size_t GetDirtyCount() const { return Dirty.GetCount(); }
  size_t GetInstanceCount() const { return Instances.size(); }
  size_t GetGreedyVertexCount() const;
  uint64_t GetMeshRevision() const { return MeshRevision; }
  uint64_t GetCullRevision() const { return CullRevision; }
  void UpdateVisibleInstances(const Frustum &frustum, const glm::mat4 &viewProj,
                              const glm::vec3 &cameraPos);
  void SetRenderSettings(const RenderSettings &settings);
  const RenderSettings &GetRenderSettings() const { return Render; }
  void SetRenderDistanceChunks(int distance)
  {
    RenderDistanceChunks = distance;
  }
  void SetAltitudeCullState(float altitude_above_terrain, int threshold_blocks)
  {
    AltitudeAboveTerrain = altitude_above_terrain;
    AltitudeFogThresholdBlocks = threshold_blocks;
  }
  void SetSurfaceWetness(float value)
  {
    SurfaceWetness = std::clamp(value, 0.0f, 1.0f);
  }
  int GetRenderDistanceChunks() const { return RenderDistanceChunks; }
  const FluidSurfaceColumnSlice *
  GetFluidSurfaceSlice(const UBlockWorld &world, UBlockRegistry &registry,
                       glm::ivec3 groundChunkCoord, int scanHintY);
  const std::unordered_set<glm::ivec3, IVec3Hash> &
  GetFluidSurfaceDirtyGroundChunks() const
  {
    return FluidSurfaceDirty;
  }
  const std::vector<FaceInstance> &GetFaceInstances() const
  {
    return Instances;
  }
  const std::vector<FaceInstance> &GetInstances() const { return Instances; }
  const std::vector<GreedyMeshBatch> &GetGreedyBatches() const
  {
    return GreedyBatches;
  }
  const std::vector<CrossInstanceBatch> &GetCrossBatches() const
  {
    return CrossBatches;
  }

private:
  struct ChunkGreedyMesh
  {
    std::vector<GreedyMeshBatch> batches;
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
  };
  void RebuildChunk(const UBlockWorld &world, UBlockRegistry &registry,
                    glm::ivec3 chunkCoord);
  void ApplyMeshResult(const UBlockWorld &world, MeshBuildResult &&result);
  void EnsureAsyncBuilder();
  void RebuildChunkLegacy(const UBlockWorld &world, UBlockRegistry &registry,
                          glm::ivec3 chunkCoord,
                          std::vector<FaceInstance> &chunkInstances);
  void RebuildFlatInstanceList(const Frustum *frustum,
                               const glm::vec3 *cameraPos,
                               float maxCullDistance);
  void RebuildFlatGreedyBatches(const Frustum *frustum,
                                const glm::vec3 *cameraPos,
                                float maxCullDistance);
  void RebuildFlatCrossInstances(const Frustum *frustum,
                                 const glm::vec3 *cameraPos,
                                 float maxCullDistance);
  void InvalidateVisibleList();
  bool UseHorizontalCullDistance() const
  {
    return AltitudeAboveTerrain >
           static_cast<float>(AltitudeFogThresholdBlocks);
  }
  std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> Cache;
  std::unordered_map<glm::ivec3, ChunkGreedyMesh, IVec3Hash> GreedyCache;
  UChunkDirtySet Dirty;
  std::vector<FaceInstance> Instances;
  std::vector<GreedyMeshBatch> GreedyBatches;
  std::vector<CrossInstanceBatch> CrossBatches;
  bool InstancesDirty{true};
  bool GreedyBatchesDirty{true};
  bool CrossBatchesDirty{true};
  uint64_t MeshRevision{0};
  uint64_t CullRevision{0};
  glm::ivec3 LastCullCameraChunk{INT32_MAX, INT32_MAX, INT32_MAX};
  uint64_t LastCullMeshRevision{0};
  uint64_t LastVisibleMeshRevision{0};
  std::vector<glm::ivec3> LastVisibleChunks;
  int RenderDistanceChunks{4};
  float AltitudeAboveTerrain{0.0f};
  int AltitudeFogThresholdBlocks{32};
  float SurfaceWetness{0.0f};
  RenderSettings Render;
  std::unique_ptr<UAsyncMeshBuilder> AsyncBuilder;
  double LastFlatRebuildMs{0.0};
  bool PendingMeshRevisionBump{false};
  std::unordered_map<glm::ivec3, FluidSurfaceColumnSlice, IVec3Hash>
      FluidSurfaceCache;
  std::unordered_set<glm::ivec3, IVec3Hash> FluidSurfaceDirty;
  void BumpMeshRevisionIfNeeded();
  void InvalidateFluidSurfaceForChunk(glm::ivec3 chunkCoord);
  void RebuildFluidSurfaceSlice(const UBlockWorld &world,
                                UBlockRegistry &registry,
                                glm::ivec3 groundChunkCoord, int scanHintY);
  size_t TotalCrossCenterCount() const;
  bool TrySkipFlatRebuildForVisibleChunks(const Frustum *frustum,
                                          const glm::vec3 *cameraPos,
                                          float maxCullDistance);
  float MaxCullDistance() const;
};
} // namespace cutum
#endif
