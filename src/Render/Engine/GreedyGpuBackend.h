#ifndef GREEDYGPUBACKEND_H
#define GREEDYGPUBACKEND_H

#include "Render/Engine/GreedyVertexPool.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include "World/Chunks/ChunkManager.h"

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

class UChunkMeshCache;

struct GreedyGpuBatch
{
  BlockId blockId{BLOCK_AIR};
  glm::ivec3 chunkCoord{0};
  /// Matches GreedyBatchRef.batchIndex for sort-order lookup without reupload.
  uint16_t batchIndex{0};
  size_t vertexCount{0};
  size_t indexCount{0};
  GLuint vbo{0};
  GLuint ebo{0};
  GLsizei indexCountGl{0};
  size_t vboCapacityBytes{0};
  size_t eboCapacityBytes{0};
  bool pooled{false};
  size_t vboByteOffset{0};
  size_t eboByteOffset{0};
  uint64_t poolAllocationId{0};
  uint32_t poolGeneration{0};
  /// Frustum sphere for instance-count cull (xyz center, w radius).
  float cullSphere[4]{0, 0, 0, 0};
  /// Exact chunk AABB for compact cull (matches Frustum::IntersectsChunkAABB).
  float cullAabbMin[3]{0, 0, 0};
  float cullAabbMax[3]{0, 0, 0};
  uint32_t drawInstanceCount{1};
};

/// Optional per-RefreshPassRefs counters (transparent Prepare binds these).
struct GreedyGpuRefreshTelem
{
  int UploadFullN{0};
  int CmdReorderN{0};
  /// 0 ok (reorder), 1 !mesh/pass_geom, 2 dirty, 3 !pool, 4 need_rebuild,
  /// 5 key_miss, 6 leftover, 7 not_attempted, 8 mesh_rev_absorb —
  /// set when sort-only was attempted or gated out.
  int OrderOnlyFailReason{0};
};

enum class TransparentOrderOnlyFailReason : int
{
  Ok = 0,
  MeshNotOk = 1,
  Dirty = 2,
  PoolNotOk = 3,
  NeedRebuild = 4,
  KeyMiss = 5,
  Leftover = 6,
  NotAttempted = 7,
  MeshRevAbsorb = 8,
};

enum class GreedyGpuPassId : uint8_t
{
  Unknown = 0,
  Opaque = 1,
  Cutout = 2,
  Transparent = 3,
};

/// Audit S2 typed publication operations (R02/R03/R04).
enum class PublicationDeltaKind : uint8_t
{
  Replace = 1,
  Remove = 2,
  RepresentationSwitch = 3,
};

/// Retained GPU buffers for greedy mesh draws (orphan + subData reuse).
struct GreedyGpuUploadInput
{
  GreedyBatchRef ref;
  const GreedyMeshBatch *batch{nullptr};
};

struct PublicationDelta
{
  PublicationDeltaKind kind{PublicationDeltaKind::Replace};
  glm::ivec3 coord{0};
  /// 0 = CPU MDI/pool table, 1 = packed GPU slot.
  uint8_t targetBackend{0};
  /// Source stamp for Replace (pass revisions at commit).
  uint64_t sourceMeshRevision{0};
  uint64_t sourceCullRevision{0};
  uint64_t sourceSortRevision{0};
  /// Replace payload (non-owning; valid for ApplyPublicationDelta duration).
  /// Empty inputs + dirty + empty pass set = authoritative empty Replace.
  const std::vector<GreedyGpuUploadInput> *replaceInputs{nullptr};
  const std::unordered_set<glm::ivec3, IVec3Hash> *replaceDirty{nullptr};
  const UChunkMeshCache *meshCache{nullptr};
  const std::unordered_set<uint16_t> *cachePassIndicesOverride{nullptr};
};

struct GreedyGpuPassCache
{
  std::vector<GreedyGpuBatch> batches;
  std::unordered_set<glm::ivec3, IVec3Hash> PendingGeometryDirty;
  uint64_t publicationVersion{0};
  GreedyGpuPassId passId{GreedyGpuPassId::Unknown};
  /// Bumped when RebuildIndirectCmdTable uploads cull/draw SSBO tables.
  uint64_t batchTableRevision{0};
  uint64_t meshRevision{0};
  uint64_t cullRevision{0};
  uint64_t sortRevision{0};
  bool usesVertexPool{false};
  GLuint poolVbo{0};
  GLuint poolEbo{0};
  UGreedyVertexPool VertexPool;
  /// Full-pass 1:1 BatchDrawRecord table (instanceCount from GPU compact).
  GLuint IndirectCmdsBuffer{0};
  size_t IndirectCmdCapacity{0};
  /// AABB min (AABB mode) or spheres (sphere mode).
  GLuint BatchSphereSsbo{0};
  size_t BatchSphereCapacity{0};
  /// AABB max corners — pass-local (M04/A02); paired with BatchSphereSsbo.
  GLuint CullAabbMaxSsbo{0};
  size_t CullAabbMaxCapacity{0};
  GLuint CullVisSsbo{0};
  size_t CullVisCapacity{0};
  bool IndirectCullReady{false};
  /// True when IndirectCmdsBuffer is authoritative for MultiDraw ranges.
  bool GpuCompactActive{false};
  /// CPU drawInstanceCount synced from CullVisSsbo (lazy, fallback draws).
  bool CompactVisCpuSynced{false};
  /// Per-pass GPU compact cull probe / fail-open history (M04/A02).
  uint64_t LastGoodCullOn{0};
  int ConsecutiveFailOpenN{0};
  int FailOpenProbeTick{0};
};

class UGreedyGpuBackend
{
public:
  // Production transaction, independent of world/cache lookup and runtime
  // tuning. Used by RefreshPassRefs and fault-injection tests alike.
  // mesh_cache: when non-null, incomplete check uses full GreedyCache pass set
  // (N01). cache_pass_indices_override: test hook simulating Append result when
  // mesh_cache is null (single dirty-coord cases).
  // Adapter: builds PublicationDelta::Replace and commits via ApplyPublicationDelta.
  bool PublishPassInputs(GreedyGpuPassCache &cache,
      const std::vector<GreedyGpuUploadInput> &inputs,
      const std::unordered_set<glm::ivec3, IVec3Hash> &dirty,
      uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision,
      const UChunkMeshCache *mesh_cache = nullptr,
      const std::unordered_set<uint16_t> *cache_pass_indices_override =
          nullptr);
  void RefreshPass(GreedyGpuPassCache &cache,
                   const std::vector<GreedyMeshBatch> &batches,
                   uint64_t mesh_revision, uint64_t cull_revision,
                   uint64_t sort_revision);
  void RefreshPassRefs(GreedyGpuPassCache &cache,
                       const UChunkMeshCache &meshCache,
                       const std::vector<GreedyBatchRef> &refs,
                       uint64_t mesh_revision, uint64_t cull_revision,
                       uint64_t sort_revision, bool consume_dirty = true);
  void DestroyPass(GreedyGpuPassCache &cache);
  void DestroyAll(GreedyGpuPassCache &opaque, GreedyGpuPassCache &cutout,
                  GreedyGpuPassCache &transparent);
  /// Audit S2: single commit path for Replace / Remove / RepresentationSwitch.
  bool ApplyPublicationDelta(GreedyGpuPassCache &cache,
                             const PublicationDelta &delta);
  /// Audit S2 RepresentationSwitch: drop MDI/pool batches for a coord (packed
  /// becomes the sole resident representation).
  void RemoveCoord(GreedyGpuPassCache &cache, glm::ivec3 coord);

  /// Last successful ApplyPublicationDelta kind (tests / diagnostics).
  PublicationDeltaKind LastAppliedDeltaKind() const
  {
    return LastAppliedDeltaKind_;
  }

  /// Bind/unbind telem sink for the next RefreshPassRefs calls (nullptr clears).
  static void BindRefreshTelem(GreedyGpuRefreshTelem *telem);

private:
  void UploadBatch(GreedyGpuBatch &gpu, const GreedyMeshBatch &batch,
                   UGreedyVertexPool &pool);
  void UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);
  void DestroyBatchBuffers(GreedyGpuBatch &batch);
  void ReleasePooledBatch(GreedyGpuBatch &batch, UGreedyVertexPool &pool);
  void FillBatchCull(GreedyGpuBatch &dst, const GreedyBatchRef &ref);

  PublicationDeltaKind LastAppliedDeltaKind_{PublicationDeltaKind::Replace};
};

/// Q5: whole-pass pool OOM retained predecessor mesh (UploadBatch / publish abort).
void NotePublicationOverloadRetain();
uint64_t ConsumePublicationOverloadRetainN();
/// N01: dirty publish missing a current GreedyCache material for the pass.
void NotePublicationIncompleteMaterial();
uint64_t ConsumePublicationIncompleteMaterialN();
/// N01: pool OOM retain path (UploadBatch / group abort).
void NotePublicationOomRetain();
uint64_t ConsumePublicationOomRetainN();
/// Q5: one successful chunk/batch publish under tiny-cap (progress unit).
void NotePublicationProgressUnit();
uint64_t ConsumePublicationProgressUnitN();
/// Honest wrong-tex thrash: Replace same geom size, different blockId (not mdi_stale).
void NotePublicationMaterialBlockIdFlip();
uint64_t ConsumePublicationMaterialBlockIdFlipN();
/// N01 autopsy: publication table changed without any_fresh (untouched/sort churn).
void NotePubVerChangedWithoutFresh();
uint64_t ConsumePubVerChangedWithoutFreshN();
/// Max pass meshRevision lag (arg − cache.meshRevision) observed this frame.
void NotePassMeshRevLag(uint64_t lag);
uint64_t ConsumePassMeshRevLagMax();

} // namespace cutum

#endif
