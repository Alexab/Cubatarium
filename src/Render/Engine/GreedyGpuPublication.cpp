#include "Render/Camera/GpuPassRefreshPolicy.h"
#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{
namespace
{

std::atomic<uint64_t> gPublicationIncompleteMaterialN{0};
std::atomic<uint64_t> gPublicationOomRetainN{0};
std::atomic<uint64_t> gPublicationProgressUnitN{0};
std::atomic<uint64_t> gPubVerChangedWithoutFreshN{0};
std::atomic<uint64_t> gPassMeshRevLagMax{0};

struct GpuBatchKey
{
  glm::ivec3 coord{0};
  uint16_t batchIndex{0};

  bool operator==(const GpuBatchKey &o) const
  {
    return coord == o.coord && batchIndex == o.batchIndex;
  }
};

struct GpuBatchKeyHash
{
  size_t operator()(const GpuBatchKey &k) const noexcept
  {
    size_t h = IVec3Hash{}(k.coord);
    h ^= static_cast<size_t>(k.batchIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

} // namespace
void UGreedyGpuBackend::DestroyBatchBuffers(GreedyGpuBatch &batch)
{
  if (batch.pooled)
  {
    batch.pooled = false;
    batch.vboByteOffset = 0;
    batch.eboByteOffset = 0;
    batch.poolAllocationId = 0;
    batch.poolGeneration = 0;
    return;
  }
  if (batch.ebo != 0)
  {
    glDeleteBuffers(1, &batch.ebo);
    batch.ebo = 0;
  }
  if (batch.vbo != 0)
  {
    glDeleteBuffers(1, &batch.vbo);
    batch.vbo = 0;
  }
  batch.vboCapacityBytes = 0;
  batch.eboCapacityBytes = 0;
}

void UGreedyGpuBackend::ReleasePooledBatch(GreedyGpuBatch &batch,
                                           UGreedyVertexPool &pool)
{
  if (batch.pooled && batch.vertexCount > 0 && batch.indexCount > 0)
  {
    GreedyGpuPoolAllocation alloc;
    alloc.vertexByteOffset = batch.vboByteOffset;
    alloc.indexByteOffset = batch.eboByteOffset;
    alloc.vertexCount = batch.vertexCount;
    alloc.indexCount = batch.indexCount;
    alloc.allocationId = batch.poolAllocationId;
    alloc.generation = batch.poolGeneration;
    pool.Free(alloc);
  }
  DestroyBatchBuffers(batch);
}

void UGreedyGpuBackend::FillBatchCull(GreedyGpuBatch &dst,
                                      const GreedyBatchRef &ref)
{
  dst.chunkCoord = ref.chunkCoord;
  dst.batchIndex = ref.batchIndex;
  FillChunkCullFields(ref.chunkCoord, dst.cullSphere, dst.cullAabbMin,
                      dst.cullAabbMax);
  dst.drawInstanceCount = 1;
}

void UGreedyGpuBackend::UploadBatch(GreedyGpuBatch &gpu,
                                    const GreedyMeshBatch &batch,
                                    UGreedyVertexPool &pool)
{
  const GreedyGpuBatch previous = gpu;
  GreedyGpuPoolAllocation prior{};
  if (gpu.pooled && gpu.vertexCount > 0 && gpu.indexCount > 0)
  {
    prior.vertexByteOffset = gpu.vboByteOffset;
    prior.indexByteOffset = gpu.eboByteOffset;
    prior.vertexCount = gpu.vertexCount;
    prior.indexCount = gpu.indexCount;
    prior.indexCountGl = gpu.indexCountGl;
    prior.allocationId = gpu.poolAllocationId;
    prior.generation = gpu.poolGeneration;
  }

  gpu.blockId = batch.blockId;
  gpu.pooled = false;
  gpu.vboByteOffset = 0;
  gpu.eboByteOffset = 0;
  gpu.poolAllocationId = 0;
  gpu.poolGeneration = 0;
  gpu.drawInstanceCount = 1;
  if (!batch.vertices.empty() && !batch.indices.empty())
  {
    const GreedyGpuPoolAllocation alloc = pool.Allocate(batch);
    if (alloc.vertexCount > 0 && alloc.indexCount > 0)
    {
      gpu.pooled = true;
      gpu.vboByteOffset = alloc.vertexByteOffset;
      gpu.eboByteOffset = alloc.indexByteOffset;
      gpu.poolAllocationId = alloc.allocationId;
      gpu.poolGeneration = alloc.generation;
      gpu.vertexCount = alloc.vertexCount;
      gpu.indexCount = alloc.indexCount;
      gpu.indexCountGl = alloc.indexCountGl;
      gpu.vbo = pool.VertexBuffer();
      gpu.ebo = pool.IndexBuffer();
      if (prior.vertexCount > 0)
      {
        pool.Free(prior);
      }
      return;
    }
  }
  if (prior.vertexCount > 0)
  {
    // Q5: tiny-cap OOM retains predecessor mesh; progress counted via
    // NotePublicationProgressUnit when sibling chunks still publish.
    NotePublicationOomRetain();
    // Retain metadata too: an allocation failure cannot change its material.
    gpu = previous;
    gpu.vbo = pool.VertexBuffer();
    gpu.ebo = pool.IndexBuffer();
    return;
  }
  gpu.vertexCount = 0;
  gpu.indexCount = 0;
  gpu.indexCountGl = 0;
}

bool UGreedyGpuBackend::PublishPassInputs(
    GreedyGpuPassCache &cache, const std::vector<GreedyGpuUploadInput> &inputs,
    const std::unordered_set<glm::ivec3, IVec3Hash> &dirty,
    uint64_t mesh_revision, uint64_t cull_revision, uint64_t sort_revision,
    const UChunkMeshCache *mesh_cache,
    const std::unordered_set<uint16_t> *cache_pass_indices_override)
{
  PublicationDelta delta;
  delta.kind = PublicationDeltaKind::Replace;
  delta.targetBackend = 0;
  delta.sourceMeshRevision = mesh_revision;
  delta.sourceCullRevision = cull_revision;
  delta.sourceSortRevision = sort_revision;
  delta.replaceInputs = &inputs;
  delta.replaceDirty = &dirty;
  delta.meshCache = mesh_cache;
  delta.cachePassIndicesOverride = cache_pass_indices_override;
  // Production: empty single-coord still commits as Replace (complete payload).
  // Remove is reserved for explicit RepresentationSwitch/RemoveCoord paths.
  return ApplyPublicationDelta(cache, delta);
}

void NotePublicationIncompleteMaterial()
{
  gPublicationIncompleteMaterialN.fetch_add(1, std::memory_order_relaxed);
}

void NotePublicationOomRetain()
{
  gPublicationOomRetainN.fetch_add(1, std::memory_order_relaxed);
}

void NotePublicationOverloadRetain()
{
  // Deprecated alias: prefer IncompleteMaterial / OomRetain. Kept for any
  // residual callers; counts as incomplete (legacy single-bucket).
  NotePublicationIncompleteMaterial();
}

void NotePublicationProgressUnit()
{
  gPublicationProgressUnitN.fetch_add(1, std::memory_order_relaxed);
}

uint64_t ConsumePublicationIncompleteMaterialN()
{
  return gPublicationIncompleteMaterialN.exchange(0, std::memory_order_relaxed);
}

uint64_t ConsumePublicationOomRetainN()
{
  return gPublicationOomRetainN.exchange(0, std::memory_order_relaxed);
}

uint64_t ConsumePublicationOverloadRetainN()
{
  // Compat: overload = incomplete + oom. Prefer split Consume* + sum at site.
  return ConsumePublicationIncompleteMaterialN() +
         ConsumePublicationOomRetainN();
}

uint64_t ConsumePublicationProgressUnitN()
{
  return gPublicationProgressUnitN.exchange(0, std::memory_order_relaxed);
}

void NotePubVerChangedWithoutFresh()
{
  gPubVerChangedWithoutFreshN.fetch_add(1, std::memory_order_relaxed);
}

uint64_t ConsumePubVerChangedWithoutFreshN()
{
  return gPubVerChangedWithoutFreshN.exchange(0, std::memory_order_relaxed);
}

void NotePassMeshRevLag(uint64_t lag)
{
  uint64_t prev = gPassMeshRevLagMax.load(std::memory_order_relaxed);
  while (lag > prev &&
         !gPassMeshRevLagMax.compare_exchange_weak(prev, lag,
                                                   std::memory_order_relaxed))
  {
  }
}

uint64_t ConsumePassMeshRevLagMax()
{
  return gPassMeshRevLagMax.exchange(0, std::memory_order_relaxed);
}

void UGreedyGpuBackend::RemoveCoord(GreedyGpuPassCache &cache,
                                    glm::ivec3 coord)
{
  PublicationDelta delta;
  delta.kind = PublicationDeltaKind::RepresentationSwitch;
  delta.coord = coord;
  delta.targetBackend = 1; // packed sole resident
  (void)ApplyPublicationDelta(cache, delta);
}

bool UGreedyGpuBackend::ApplyPublicationDelta(GreedyGpuPassCache &cache,
                                              const PublicationDelta &delta)
{
  if (delta.kind == PublicationDeltaKind::Replace)
  {
    if (!delta.replaceInputs || !delta.replaceDirty)
      return false;
    const std::vector<GreedyGpuUploadInput> &inputs = *delta.replaceInputs;
    const std::unordered_set<glm::ivec3, IVec3Hash> &dirty = *delta.replaceDirty;
    const uint64_t mesh_revision = delta.sourceMeshRevision;
    const uint64_t cull_revision = delta.sourceCullRevision;
    const uint64_t sort_revision = delta.sourceSortRevision;
    const UChunkMeshCache *mesh_cache = delta.meshCache;
    const std::unordered_set<uint16_t> *cache_pass_indices_override =
        delta.cachePassIndicesOverride;

  cache.PendingGeometryDirty.insert(dirty.begin(), dirty.end());
  std::unordered_map<GpuBatchKey, size_t, GpuBatchKeyHash> resident;
  for (size_t n = 0; n < cache.batches.size(); ++n)
    resident.emplace(
        GpuBatchKey{cache.batches[n].chunkCoord, cache.batches[n].batchIndex},
        n);

  // Transaction unit is chunk+pass (audit N01 v2): stage all uploads for a coord
  // and commit only when every batch succeeds. Neighbors progress independently.
  // Partial OOM keeps PendingGeometryDirty and all predecessors for that coord;
  // pass mesh/cull/sort revisions advance only when every upload coord commits.
  std::vector<GreedyGpuBatch> staged;
  std::vector<size_t> fresh;
  std::vector<bool> retained(cache.batches.size(), false);
  staged.reserve(inputs.size());
  bool changed = cache.sortRevision != sort_revision;
  bool any_fresh = false;
  bool any_fail = false;
  std::unordered_set<glm::ivec3, IVec3Hash> published_ok;
  std::unordered_map<glm::ivec3, std::vector<const GreedyGpuUploadInput *>,
                     IVec3Hash>
      uploads_by_coord;
  std::vector<glm::ivec3> upload_order;

  auto sync_handles = [&]()
  {
    cache.poolVbo = cache.VertexPool.VertexBuffer();
    cache.poolEbo = cache.VertexPool.IndexBuffer();
    for (auto &gpu : cache.batches)
      if (gpu.pooled)
      {
        gpu.vbo = cache.poolVbo;
        gpu.ebo = cache.poolEbo;
      }
  };

  const bool transparent_pass = cache.passId == GreedyGpuPassId::Transparent;

  for (const auto &input : inputs)
  {
    const auto &ref = input.ref;
    const auto *batch = input.batch;
    if (!batch || batch->vertices.empty() || batch->indices.empty())
      continue;
    const auto found = resident.find({ref.chunkCoord, ref.batchIndex});
    if (found != resident.end() &&
        cache.PendingGeometryDirty.count(ref.chunkCoord) == 0 &&
        cache.batches[found->second].pooled)
    {
      retained[found->second] = true;
      if (found->second != staged.size())
        changed = true;
      staged.push_back(cache.batches[found->second]);
      if (BatchCullAabbDegenerate(staged.back().cullAabbMin,
                                  staged.back().cullAabbMax))
      {
        FillBatchCull(staged.back(), ref);
        changed = true;
      }
      continue;
    }
    auto &group = uploads_by_coord[ref.chunkCoord];
    if (group.empty())
      upload_order.push_back(ref.chunkCoord);
    group.push_back(&input);
  }

  for (const auto &coord : upload_order)
  {
    const auto &group = uploads_by_coord[coord];
    std::vector<GreedyGpuBatch> group_fresh;
    group_fresh.reserve(group.size());
    bool group_ok = true;
    const bool coord_dirty = cache.PendingGeometryDirty.count(coord) > 0;
    std::unordered_set<uint16_t> upload_batch_indices;
    for (const auto *input : group)
      upload_batch_indices.insert(input->ref.batchIndex);
    // N01: require cache pass materials ⊆ upload (not resident ⊆ upload).
    // Material remove: resident extras drop on successful group commit.
    if (coord_dirty)
    {
      std::unordered_set<uint16_t> cache_pass_indices;
      bool have_cache_set = false;
      if (mesh_cache != nullptr && cache.passId != GreedyGpuPassId::Unknown)
      {
        std::vector<GreedyBatchRef> cache_refs;
        mesh_cache->AppendGreedyPassBatchRefs(coord, transparent_pass,
                                              cache_refs);
        for (const auto &cref : cache_refs)
          cache_pass_indices.insert(cref.batchIndex);
        have_cache_set = true;
      }
      else if (cache_pass_indices_override != nullptr)
      {
        cache_pass_indices = *cache_pass_indices_override;
        have_cache_set = true;
      }
      if (have_cache_set)
      {
        for (const uint16_t idx : cache_pass_indices)
        {
          if (upload_batch_indices.count(idx) == 0)
          {
            group_ok = false;
            any_fail = true;
            NotePublicationIncompleteMaterial();
            break;
          }
        }
      }
    }
    for (const auto *input : group)
    {
      if (!group_ok)
        break;
      GreedyGpuBatch gpu;
      UploadBatch(gpu, *input->batch, cache.VertexPool);
      if (!gpu.pooled)
      {
        group_ok = false;
        any_fail = true;
        NotePublicationOomRetain();
        break;
      }
      FillBatchCull(gpu, input->ref);
      group_fresh.push_back(std::move(gpu));
    }
    if (!group_ok)
    {
      for (auto &gpu : group_fresh)
        ReleasePooledBatch(gpu, cache.VertexPool);
      // Keep the full predecessor image for this coord.
      for (size_t n = 0; n < cache.batches.size(); ++n)
      {
        if (cache.batches[n].chunkCoord == coord && cache.batches[n].pooled)
        {
          retained[n] = true;
          staged.push_back(cache.batches[n]);
        }
      }
      continue;
    }
    any_fresh = true;
    NotePublicationProgressUnit();
    published_ok.insert(coord);
    // Successful replace: do not retain old batches for this coord.
    for (size_t n = 0; n < cache.batches.size(); ++n)
    {
      if (cache.batches[n].chunkCoord == coord)
        retained[n] = false;
    }
    for (auto &gpu : group_fresh)
    {
      fresh.push_back(staged.size());
      staged.push_back(std::move(gpu));
      changed = true;
    }
  }

  if (any_fail && !any_fresh)
  {
    // Zero progress: keep prior publication + accumulated dirty demand.
    for (const size_t n : fresh)
      ReleasePooledBatch(staged[n], cache.VertexPool);
    sync_handles();
    return false;
  }

  // N01 v2.1: coords with no inputs in this call keep their resident batches.
  // Otherwise neighbor progress frees still-dirty peers (mixed pass / holes).
  // Audit R01: retained[n] must stay true so ReleasePooledBatch does not Free
  // ranges still referenced by the published draw table.
  // Audit R02: dirty + authoritative empty pass set = empty Replace / Remove.
  std::unordered_set<glm::ivec3, IVec3Hash> handled_coords;
  for (const auto &input : inputs)
  {
    if (!input.batch || input.batch->vertices.empty() ||
        input.batch->indices.empty())
      continue;
    handled_coords.insert(input.ref.chunkCoord);
  }
  auto resolve_cache_pass_indices =
      [&](const glm::ivec3 &coord, std::unordered_set<uint16_t> &out) -> bool
  {
    if (mesh_cache != nullptr && cache.passId != GreedyGpuPassId::Unknown)
    {
      std::vector<GreedyBatchRef> cache_refs;
      mesh_cache->AppendGreedyPassBatchRefs(coord, transparent_pass,
                                            cache_refs);
      for (const auto &cref : cache_refs)
        out.insert(cref.batchIndex);
      LastAppliedDeltaKind_ = PublicationDeltaKind::Replace;
  return true;
    }
    if (cache_pass_indices_override != nullptr)
    {
      // Test hook: single-dirty override applies to each dirty coord in the call.
      out = *cache_pass_indices_override;
      LastAppliedDeltaKind_ = PublicationDeltaKind::Replace;
  return true;
    }
    return false;
  };
  for (const auto &coord : dirty)
  {
    if (handled_coords.count(coord) > 0 || uploads_by_coord.count(coord) > 0)
      continue;
    std::unordered_set<uint16_t> cache_pass_indices;
    if (!resolve_cache_pass_indices(coord, cache_pass_indices))
      continue;
    if (!cache_pass_indices.empty())
      continue;
    // Authoritative empty payload: drop resident batches for this coord.
    for (size_t n = 0; n < cache.batches.size(); ++n)
    {
      if (cache.batches[n].chunkCoord == coord)
        retained[n] = false;
    }
    handled_coords.insert(coord);
    published_ok.insert(coord);
    any_fresh = true;
    changed = true;
    NotePublicationProgressUnit();
  }
  for (size_t n = 0; n < cache.batches.size(); ++n)
  {
    const auto &batch = cache.batches[n];
    if (handled_coords.count(batch.chunkCoord) > 0)
      continue;
    retained[n] = true;
    staged.push_back(batch);
    changed = true;
  }

  // Detect resident-table membership/order change before swap (R04).
  bool table_identity_changed = staged.size() != cache.batches.size();
  if (!table_identity_changed)
  {
    for (size_t i = 0; i < staged.size(); ++i)
    {
      if (staged[i].chunkCoord != cache.batches[i].chunkCoord ||
          staged[i].batchIndex != cache.batches[i].batchIndex)
      {
        table_identity_changed = true;
        break;
      }
    }
  }

  changed = changed || staged.size() != cache.batches.size();
  for (size_t n = 0; n < cache.batches.size(); ++n)
    if (!retained[n])
      ReleasePooledBatch(cache.batches[n], cache.VertexPool);
  cache.batches = std::move(staged);
  // Clear dirty only for chunk groups fully published this call. Never wipe
  // unrelated pending demand (neighbor progress must not clear a failed peer).
  for (const auto &coord : published_ok)
    cache.PendingGeometryDirty.erase(coord);
  cache.usesVertexPool = !cache.batches.empty();
  sync_handles();
  // N01 v2 / epoch-split: do not advance pass revisions while any group failed,
  // any upload coord remains unpublished, or PendingGeometryDirty remains —
  // otherwise consumers treat a mixed pass as done. publicationVersion still
  // advances on geometry change (see below).
  bool all_upload_coords_published = true;
  for (const auto &coord : upload_order)
  {
    if (published_ok.count(coord) == 0)
    {
      all_upload_coords_published = false;
      break;
    }
  }
  const bool advance_pass_revisions =
      !any_fail && all_upload_coords_published &&
      cache.PendingGeometryDirty.empty();
  if (advance_pass_revisions)
  {
    cache.meshRevision = mesh_revision;
    cache.cullRevision = cull_revision;
    cache.sortRevision = sort_revision;
  }
  if (changed)
  {
    cache.IndirectCullReady = false;
    cache.GpuCompactActive = false;
    cache.CompactVisCpuSynced = false;
    // R04: bump on fresh upload OR resident table membership/order change.
    // Untouched-only retention with identical table identity does not bump
    // (keeps 121131 thrash class closed).
    if (any_fresh || table_identity_changed)
      ++cache.publicationVersion;
    else
      NotePubVerChangedWithoutFresh();
  }
  cache.VertexPool.SignalUploadComplete();
  LastAppliedDeltaKind_ = PublicationDeltaKind::Replace;
  return true;

  }
  if (delta.kind != PublicationDeltaKind::Remove &&
      delta.kind != PublicationDeltaKind::RepresentationSwitch)
  {
    return false;
  }
  if (cache.batches.empty())
  {
    cache.PendingGeometryDirty.erase(delta.coord);
    LastAppliedDeltaKind_ = delta.kind;
    return false;
  }
  std::vector<GreedyGpuBatch> kept;
  kept.reserve(cache.batches.size());
  bool removed = false;
  for (GreedyGpuBatch &batch : cache.batches)
  {
    if (batch.chunkCoord == delta.coord)
    {
      ReleasePooledBatch(batch, cache.VertexPool);
      DestroyBatchBuffers(batch);
      removed = true;
      continue;
    }
    kept.push_back(std::move(batch));
  }
  if (!removed)
  {
    cache.PendingGeometryDirty.erase(delta.coord);
    return false;
  }
  cache.batches = std::move(kept);
  cache.PendingGeometryDirty.erase(delta.coord);
  cache.usesVertexPool = !cache.batches.empty();
  cache.IndirectCullReady = false;
  cache.GpuCompactActive = false;
  cache.CompactVisCpuSynced = false;
  ++cache.publicationVersion;
  cache.poolVbo = cache.VertexPool.VertexBuffer();
  cache.poolEbo = cache.VertexPool.IndexBuffer();
  for (auto &gpu : cache.batches)
  {
    if (gpu.pooled)
    {
      gpu.vbo = cache.poolVbo;
      gpu.ebo = cache.poolEbo;
    }
  }
  LastAppliedDeltaKind_ = delta.kind;
  return true;
}

} // namespace cutum
