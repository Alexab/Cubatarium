#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace
{
GLuint nextBuffer = 1;
uintptr_t nextFence = 1;
std::unordered_map<GLuint, std::vector<unsigned char>> buffers;
std::unordered_map<GLenum, GLuint> bound;
std::unordered_map<uintptr_t, GLenum> fences;
bool failFence = false;
void GLAPIENTRY Gen(GLsizei n, GLuint *out)
{
  while (n--)
  {
    *out = nextBuffer++;
    buffers[*out++] = {};
  }
}
void GLAPIENTRY Bind(GLenum target, GLuint id) { bound[target] = id; }
void GLAPIENTRY Data(GLenum target, GLsizeiptr size, const void *data, GLenum)
{
  auto &b = buffers[bound[target]];
  b.resize(static_cast<size_t>(size));
  if (data)
    std::memcpy(b.data(), data, static_cast<size_t>(size));
}
void GLAPIENTRY SubData(GLenum target, GLintptr offset, GLsizeiptr size,
                        const void *data)
{
  std::memcpy(buffers.at(bound[target]).data() + offset, data,
              static_cast<size_t>(size));
}
void GLAPIENTRY Size(GLenum target, GLenum, GLint64 *out)
{
  *out = buffers.at(bound[target]).size();
}
void GLAPIENTRY Copy(GLenum read, GLenum write, GLintptr r, GLintptr w,
                     GLsizeiptr n)
{
  std::memcpy(buffers.at(bound[write]).data() + w,
              buffers.at(bound[read]).data() + r, static_cast<size_t>(n));
}
void GLAPIENTRY Delete(GLsizei n, const GLuint *ids)
{
  while (n--)
    buffers.erase(*ids++);
}
void *GLAPIENTRY Map(GLenum, GLintptr, GLsizeiptr, GLbitfield)
{
  return nullptr;
}
GLboolean GLAPIENTRY Unmap(GLenum) { return GL_TRUE; }
GLsync GLAPIENTRY Fence(GLenum, GLbitfield)
{
  if (failFence)
    return nullptr;
  auto id = nextFence++;
  fences[id] = GL_TIMEOUT_EXPIRED;
  return reinterpret_cast<GLsync>(id);
}
GLenum GLAPIENTRY Wait(GLsync sync, GLbitfield, GLuint64 timeout)
{
  if (timeout)
    std::abort(); // Production polling must not block a frame.
  return fences.at(reinterpret_cast<uintptr_t>(sync));
}
void GLAPIENTRY DeleteSync(GLsync sync)
{
  fences.erase(reinterpret_cast<uintptr_t>(sync));
}
} // namespace

int RunGreedyPoolDriverTest();
int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--driver") == 0)
    return RunGreedyPoolDriverTest();
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
  int failures = 0;
  auto check = [&](bool ok, const char *what)
  {
    if (!ok)
    {
      std::cerr << "FAIL: " << what << '\n';
      ++failures;
    }
  };
  cutum::UGreedyVertexPool pool;
  cutum::GreedyMeshBatch batch;
  batch.vertices.resize(4);
  batch.indices = {0, 1, 2};
  std::memset(batch.vertices.data(), 0x5A,
              batch.vertices.size() * sizeof(cutum::GreedyMeshVertex));
  auto first = pool.Allocate(batch);
  check(first.vertexCount == 4, "initial allocation");
  const auto bytes = buffers.at(pool.VertexBuffer());
  auto second = pool.Allocate(batch); // Growth must preserve the first mesh.
  check(std::equal(bytes.begin(), bytes.end(),
                   buffers.at(pool.VertexBuffer()).begin()),
        "growth preserves resident contents");
  pool.SignalDrawComplete(); // F1 pending
  pool.Free(first);
  pool.SignalDrawComplete(); // F2 pending; must not destroy F1 proof
  pool.BeginUploadFrame();
  check(pool.FreeSlotCount() == 0 && pool.RetiredSlotCount() == 1,
        "older pending fence never means signaled");
  check(!pool.Reserve(1, 1), "reserve cannot reset live/retired ranges");
  fences.at(1) = GL_ALREADY_SIGNALED;
  pool.BeginUploadFrame();
  check(pool.FreeSlotCount() == 1, "reclaim only after completion");
  pool.Free(second); // F2 pending
  fences.at(2) = GL_WAIT_FAILED;
  pool.BeginUploadFrame();
  check(pool.RetiredSlotCount() == 1, "wait failure retains allocation");
  const auto capacity = pool.CapacityBytes();
  const auto vbo = pool.VertexBuffer();
  pool.SetMaxCapacityBytes(capacity);
  check(!pool.EnsureMinCapacity(capacity + 1, 1), "tiny cap rejects growth");
  check(pool.VertexBuffer() == vbo && pool.CapacityBytes() == capacity,
        "cap rejection leaves storage intact");
  auto reused = pool.Allocate(batch);
  failFence = true;
  pool.SignalDrawComplete();
  pool.Free(reused);
  pool.BeginUploadFrame();
  check(pool.RetiredSlotCount() == 2, "null fence cannot authorize reuse");
  pool.Destroy();
  // Exercise the same publication transaction called by RefreshPassRefs.
  failFence = false;
  cutum::UGreedyGpuBackend backend;
  cutum::GreedyGpuPassCache cache;
  cutum::GreedyBatchRef a{}, b{};
  a.chunkCoord = b.chunkCoord = {2, 0, 3};
  a.batchIndex = 0;
  b.batchIndex = 1;
  check(
      backend.PublishPassInputs(cache, {{a, &batch}, {b, &batch}}, {}, 1, 1, 1),
      "initial publication");
  cache.VertexPool.SignalDrawComplete();
  check(backend.PublishPassInputs(cache, {{a, &batch}}, {}, 2, 2, 1),
        "remove one material batch from resident chunk");
  check(cache.batches.size() == 1 && cache.batches[0].batchIndex == 0,
        "removed batch is not a ghost draw");
  const auto published_offset = cache.batches[0].vboByteOffset;
  const auto publication_version = cache.publicationVersion;
  auto replacement = batch;
  replacement.blockId = static_cast<cutum::BlockId>(9);
  replacement.vertices.resize(100);
  cache.VertexPool.SetMaxCapacityBytes(cache.VertexPool.CapacityBytes());
  check(!backend.PublishPassInputs(cache, {{a, &replacement}}, {a.chunkCoord},
                                   3, 3, 1),
        "replacement defers at cap");
  check(cache.meshRevision == 2 &&
            cache.publicationVersion == publication_version &&
            cache.batches[0].vboByteOffset == published_offset &&
            cache.PendingGeometryDirty.count(a.chunkCoord) == 1,
        "failure retains published geometry, version and demand");
  cache.VertexPool.SetMaxCapacityBytes(0);
  for (auto &f : fences)
    f.second = GL_ALREADY_SIGNALED;
  cache.VertexPool.BeginUploadFrame();
  check(backend.PublishPassInputs(cache, {{a, &replacement}}, {}, 3, 3, 1),
        "retained demand retries without another dirty event");
  check(cache.batches[0].blockId == replacement.blockId &&
            cache.PendingGeometryDirty.empty(),
        "successful replacement publishes atomically");
  cache.batches[0].drawInstanceCount = 0;
  const auto stable_version = cache.publicationVersion;
  check(backend.PublishPassInputs(cache, {{a, &replacement}}, {}, 3, 3, 1) &&
            cache.batches[0].drawInstanceCount == 0 &&
            cache.publicationVersion == stable_version,
        "unchanged table retains cull state");
  cache.VertexPool.Destroy();

  // N01: multi-batch partial OOM must keep dirty and full old A/B (no mixed versions).
  {
    cutum::UGreedyGpuBackend backend2;
    cutum::GreedyGpuPassCache cache2;
    cutum::GreedyBatchRef ra{}, rb{};
    ra.chunkCoord = rb.chunkCoord = {4, 0, 5};
    ra.batchIndex = 0;
    rb.batchIndex = 1;
    cutum::GreedyMeshBatch ba = batch;
    cutum::GreedyMeshBatch bb = batch;
    ba.blockId = static_cast<cutum::BlockId>(9);
    bb.blockId = static_cast<cutum::BlockId>(9);
    check(backend2.PublishPassInputs(cache2, {{ra, &ba}, {rb, &bb}}, {}, 1, 1,
                                     1),
          "N01 initial A/B publication");
    check(cache2.batches.size() == 2, "N01 two resident batches");
    auto small_a = ba;
    auto big_b = bb;
    small_a.blockId = static_cast<cutum::BlockId>(18);
    small_a.vertices.resize(4);
    big_b.blockId = static_cast<cutum::BlockId>(19);
    big_b.vertices.resize(120);
    cache2.VertexPool.SetMaxCapacityBytes(cache2.VertexPool.CapacityBytes());
    (void)backend2.PublishPassInputs(cache2, {{ra, &small_a}, {rb, &big_b}},
                                     {ra.chunkCoord}, 2, 2, 1);
    check(cache2.PendingGeometryDirty.count(ra.chunkCoord) == 1,
          "N01 partial OOM keeps chunk dirty");
    uint16_t a_id = 0;
    uint16_t b_id = 0;
    for (const auto &g : cache2.batches)
    {
      if (g.chunkCoord == ra.chunkCoord && g.batchIndex == 0)
        a_id = g.blockId;
      if (g.chunkCoord == ra.chunkCoord && g.batchIndex == 1)
        b_id = g.blockId;
    }
    check(a_id == 9 && b_id == 9,
          "N01 partial OOM keeps full old A/B (no mixed versions)");
    check(cache2.batches.size() == 2, "N01 still two predecessor batches");

    // Neighbor chunk can publish while primary stays dirty.
    cutum::GreedyBatchRef rn{};
    rn.chunkCoord = {9, 0, 5};
    rn.batchIndex = 0;
    cutum::GreedyMeshBatch bn = batch;
    bn.blockId = static_cast<cutum::BlockId>(7);
    for (auto &f : fences)
      f.second = GL_ALREADY_SIGNALED;
    cache2.VertexPool.BeginUploadFrame();
    cache2.VertexPool.SetMaxCapacityBytes(0);
    check(backend2.PublishPassInputs(cache2, {{rn, &bn}}, {rn.chunkCoord}, 3, 3,
                                     1),
          "N01 neighbor publishes independently");
    check(cache2.PendingGeometryDirty.count(ra.chunkCoord) == 1,
          "N01 primary dirty survives neighbor publish");
    bool neighbor_ok = false;
    for (const auto &g : cache2.batches)
      if (g.chunkCoord == rn.chunkCoord && g.blockId == 7)
        neighbor_ok = true;
    check(neighbor_ok, "N01 neighbor batch resident");
    cache2.VertexPool.Destroy();
  }
  return failures ? 1 : 0;
}
