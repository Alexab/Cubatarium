// Audit-only: real allocator/publication, existing GL storage/fence mock.
// Exit 1 means correctness violated, NOT that reproduction failed to run.
// Does not launch the game, load a world, or change production sources.
#define main ExistingProductionSuiteMain
#include "../../../src/Test/GreedyVertexPoolProductionTest.cpp"
#undef main
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
    // Authoritative replacement with zero batches (remove last material).
    backend.PublishPassInputs(cache, {}, {ar.chunkCoord}, 2, 2, 1, nullptr, &empty_pass);
    const bool stuck = !cache.batches.empty() &&
                       cache.PendingGeometryDirty.count(ar.chunkCoord) != 0 &&
                       cache.meshRevision == 1;
    std::cout << "empty_replacement_retains_ghost_and_dirty=" << stuck << '\n';
    violations += stuck;
    cache.VertexPool.Destroy();
  }
  {
    cutum::GreedyGpuPassCache cache;
    backend.PublishPassInputs(cache, {{ar, &a}, {br, &b}}, {}, 1, 1, 1);
    const auto epoch = cache.publicationVersion;
    backend.PublishPassInputs(cache, {{br, &b}, {ar, &a}}, {}, 1, 1, 2);
    const bool same_epoch = cache.batches[0].chunkCoord == br.chunkCoord &&
                            cache.publicationVersion == epoch;
    std::cout << "reordered_table_keeps_publication_epoch=" << same_epoch << '\n';
    violations += same_epoch;
    cache.VertexPool.Destroy();
  }
  std::cout << "correctness_violations=" << violations << '\n';
  return violations ? 1 : 0;
}
