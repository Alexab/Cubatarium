// Audit-only: real allocator/publication, existing GL storage/fence mock.
// Exit 1 means correctness violated, NOT that reproduction failed to run.
// Does not launch the game, load a world, or change production sources.
#define main ExistingProductionSuiteMain
#include "../../../src/Test/GreedyVertexPoolProductionTest.cpp"
#undef main
#include "Render/Engine/GreedyPassBatchRefs.h"
int RunGreedyPoolDriverTest() { return 77; }

int main()
{
  __glewGenBuffers = Gen;
  __glewBindBuffer = Bind;
  __glewBufferData = Data;
  __glewBufferSubData = SubData;
  __glewGetBufferParameteri64v = Size;
  __glewCopyBufferSubData = Copy;
  __glewDeleteBuffers = Delete;
  __glewMapBufferRange = Map;
  __glewUnmapBuffer = Unmap;
  __glewFenceSync = Fence;
  __glewClientWaitSync = Wait;
  __glewDeleteSync = DeleteSync;
  auto complete = [] { for (auto &f : fences) f.second = GL_ALREADY_SIGNALED; };
  int violations = 0;
  cutum::UGreedyGpuBackend backend;
  cutum::GreedyMeshBatch a;
  a.vertices.resize(4);
  a.indices = {0, 1, 2};
  a.blockId = 8;
  std::memset(a.vertices.data(), 0x5A, a.vertices.size() * sizeof(cutum::GreedyMeshVertex));
  auto b = a;
  b.blockId = 9;
  std::memset(b.vertices.data(), 0xA5, b.vertices.size() * sizeof(cutum::GreedyMeshVertex));
  cutum::GreedyBatchRef ar{}, br{};
  ar.chunkCoord = {2, 0, 3};
  br.chunkCoord = {3, 0, 3};
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}, {br, &b}}, {}, 1, 1, 1);
    const auto offset = cache.batches[0].vboByteOffset;
    cache.VertexPool.SignalDrawComplete();
    // A omitted while B remains visible: ordinary non-empty production call.
    backend.PublishPassInputs(cache, {{br, &b}}, {}, 1, 1, 1);
    complete();
    cache.VertexPool.BeginUploadFrame();
    const auto next = cache.VertexPool.Allocate(b);
    bool alias = false;
    for (const auto &draw : cache.batches)
      if (draw.chunkCoord == ar.chunkCoord && draw.pooled &&
          draw.vboByteOffset == next.vertexByteOffset) alias = true;
    const bool overwritten = buffers.at(cache.VertexPool.VertexBuffer()).at(offset) == 0xA5;
    std::cout << "retained_batch_aliases_new_allocation=" << alias
              << " retained_bytes_overwritten=" << overwritten << '\n';
    violations += alias && overwritten;
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}, {br, &b}}, {}, 1, 1, 1);
    cache.VertexPool.SignalDrawComplete();
    backend.PublishPassInputs(cache, {{br, &b}}, {}, 1, 1, 1);
    complete();
    cache.VertexPool.BeginUploadFrame();
    backend.PublishPassInputs(cache, {{br, &b}}, {}, 1, 1, 1);
    const auto x = cache.VertexPool.Allocate(a);
    const auto y = cache.VertexPool.Allocate(b);
    const bool overlap = x.vertexByteOffset == y.vertexByteOffset &&
                         x.indexByteOffset == y.indexByteOffset;
    std::cout << "repeated_omission_two_live_allocations_overlap=" << overlap << '\n';
    violations += overlap;
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}}, {}, 1, 1, 1);
    const std::unordered_set<uint16_t> empty_pass;
    backend.PublishPassInputs(cache, {}, {ar.chunkCoord}, 2, 2, 1, nullptr, &empty_pass);
    const bool stuck = !cache.batches.empty() &&
                       cache.PendingGeometryDirty.count(ar.chunkCoord) != 0 &&
                       cache.meshRevision == 1;
    const bool replace_kind =
        backend.LastAppliedDeltaKind() == cutum::PublicationDeltaKind::Replace;
    std::cout << "empty_replacement_retains_ghost_and_dirty=" << stuck
              << " empty_via_replace_kind=" << replace_kind << '\n';
    violations += stuck;
    violations += !replace_kind;
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyMeshBatch live_batch = a;
    live_batch.Transparent = false;
    std::vector<cutum::GreedyBatchRef> refs;
    cutum::AppendGreedyPassBatchRefsFromBatches(ar.chunkCoord, false,
                                                {live_batch}, refs);
    const bool live_ok = refs.size() == 1 && refs[0].chunkCoord == ar.chunkCoord;
    std::vector<cutum::GreedyBatchRef> empty_refs;
    cutum::AppendGreedyPassBatchRefsFromBatches(ar.chunkCoord, false, {},
                                                empty_refs);
    std::cout << "live_append_helper_ok=" << (live_ok && empty_refs.empty())
              << '\n';
    violations += !(live_ok && empty_refs.empty());
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}, {br, &b}}, {}, 1, 1, 2);
    const auto epoch = cache.publicationVersion;
    backend.PublishPassInputs(cache, {{br, &b}, {ar, &a}}, {}, 1, 1, 2);
    const bool same_epoch = cache.batches[0].chunkCoord == br.chunkCoord &&
                            cache.publicationVersion == epoch;
    std::cout << "reordered_table_keeps_publication_epoch=" << same_epoch << '\n';
    violations += same_epoch;
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}}, {}, 1, 1, 1);
    const bool had = !cache.batches.empty();
    cutum::PublicationDelta sw;
    sw.kind = cutum::PublicationDeltaKind::RepresentationSwitch;
    sw.coord = ar.chunkCoord;
    sw.targetBackend = 1;
    const bool applied = backend.ApplyPublicationDelta(cache, sw);
    bool ghost = false;
    for (const auto &draw : cache.batches)
      if (draw.chunkCoord == ar.chunkCoord)
        ghost = true;
    const bool kind_ok =
        backend.LastAppliedDeltaKind() ==
        cutum::PublicationDeltaKind::RepresentationSwitch;
    std::cout << "representation_switch_clears_mdi="
              << (had && applied && !ghost && kind_ok) << '\n';
    violations += !(had && applied && !ghost && kind_ok);
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}}, {}, 1, 1, 1);
    cutum::PublicationDelta rm;
    rm.kind = cutum::PublicationDeltaKind::Remove;
    rm.coord = ar.chunkCoord;
    const bool applied = backend.ApplyPublicationDelta(cache, rm);
    bool ghost = false;
    for (const auto &draw : cache.batches)
      if (draw.chunkCoord == ar.chunkCoord)
        ghost = true;
    const bool kind_ok =
        backend.LastAppliedDeltaKind() == cutum::PublicationDeltaKind::Remove;
    std::cout << "explicit_remove_clears_mdi="
              << (applied && !ghost && kind_ok) << '\n';
    violations += !(applied && !ghost && kind_ok);
    cache.VertexPool.Destroy();
  }
  {
    // R01 generation ledger: stale Free (wrong generation) must not drop live.
    cutum::UGreedyVertexPool pool;
    cutum::GreedyMeshBatch batch;
    batch.vertices.resize(4);
    batch.indices = {0, 1, 2};
    std::memset(batch.vertices.data(), 0x11,
                batch.vertices.size() * sizeof(cutum::GreedyMeshVertex));
    auto live = pool.Allocate(batch);
    cutum::GreedyGpuPoolAllocation stale = live;
    stale.generation = live.generation + 1;
    pool.Free(stale);
    const bool stale_counted = pool.ConsumeDoubleFreeN() == 1;
    const bool still_live = pool.DebugLiveFreeRetiredDisjoint();
    // Live must still own the range — Free with matching handle succeeds.
    pool.Free(live);
    const bool freed_ok = pool.ConsumeDoubleFreeN() == 0;
    const bool disjoint_after = pool.DebugLiveFreeRetiredDisjoint();
    // Reuse bumps generation; old handle Free is rejected.
    auto reused = pool.Allocate(batch);
    pool.Free(live);
    const bool old_handle_rejected = pool.ConsumeDoubleFreeN() == 1;
    pool.Free(reused);
    const bool model_ok =
        stale_counted && still_live && freed_ok && disjoint_after &&
        old_handle_rejected && pool.DebugLiveFreeRetiredDisjoint() &&
        reused.generation > live.generation &&
        reused.allocationId != live.allocationId;
    std::cout << "generation_ledger_rejects_stale_free=" << model_ok << '\n';
    violations += !model_ok;
    pool.Destroy();
  }
  std::cout << "correctness_violations=" << violations << '\n';
  return violations ? 1 : 0;
}
