#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "glog/logging.h"
#include <cstring>

namespace cutum
{

UMdiVertexPoolStore::~UMdiVertexPoolStore()
{
  if (IndirectBuffer != 0)
  {
    glDeleteBuffers(1, &IndirectBuffer);
    IndirectBuffer = 0;
  }
  if (MappedVbo != 0)
  {
    glDeleteBuffers(1, &MappedVbo);
    MappedVbo = 0;
  }
}

size_t UMdiVertexPoolStore::BuildIndirectCommandsRange(
    const GreedyGpuPassCache &cache, size_t begin, size_t end,
    std::vector<DrawElementsIndirectCommand> &out)
{
  out.clear();
  if (!cache.usesVertexPool || cache.poolVbo == 0 || cache.poolEbo == 0)
  {
    return 0;
  }
  if (begin >= end || end > cache.batches.size())
  {
    return 0;
  }
  out.reserve(end - begin);
  for (size_t i = begin; i < end; ++i)
  {
    const GreedyGpuBatch &gpu = cache.batches[i];
    if (!gpu.pooled || gpu.indexCountGl <= 0)
    {
      continue;
    }
    DrawElementsIndirectCommand cmd;
    cmd.count = static_cast<uint32_t>(gpu.indexCountGl);
    cmd.instanceCount = 1;
    cmd.firstIndex =
        static_cast<uint32_t>(gpu.eboByteOffset / sizeof(uint32_t));
    cmd.baseVertex =
        static_cast<int32_t>(gpu.vboByteOffset / sizeof(GreedyMeshVertex));
    cmd.baseInstance = 0;
    out.push_back(cmd);
  }
  return out.size();
}

size_t UMdiVertexPoolStore::BuildIndirectCommands(
    const GreedyGpuPassCache &cache,
    std::vector<DrawElementsIndirectCommand> &out)
{
  return BuildIndirectCommandsRange(cache, 0, cache.batches.size(), out);
}

bool UMdiVertexPoolStore::SubmitIndirectCommands(
    const std::vector<DrawElementsIndirectCommand> &cmds)
{
  LastDrawCmds = 0;
  if (cmds.empty())
  {
    return false;
  }

#if defined(GL_DRAW_INDIRECT_BUFFER)
  const size_t bytes = cmds.size() * sizeof(DrawElementsIndirectCommand);
  if (IndirectBuffer == 0)
  {
    glGenBuffers(1, &IndirectBuffer);
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
  if (bytes > IndirectCapacityBytes)
  {
    glBufferData(GL_DRAW_INDIRECT_BUFFER, static_cast<GLsizeiptr>(bytes),
                 cmds.data(), GL_DYNAMIC_DRAW);
    IndirectCapacityBytes = bytes;
  }
  else
  {
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                    cmds.data());
  }

#if defined(GL_ARB_multi_draw_indirect) || defined(glMultiDrawElementsIndirect)
  glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                              static_cast<GLsizei>(cmds.size()), 0);
#else
  for (size_t i = 0; i < cmds.size(); ++i)
  {
    glDrawElementsIndirect(
        GL_TRIANGLES, GL_UNSIGNED_INT,
        reinterpret_cast<void *>(i * sizeof(DrawElementsIndirectCommand)));
  }
#endif
  LastDrawCmds = cmds.size();
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  return true;
#else
  (void)cmds;
  LOG_FIRST_N(WARNING, 1)
      << "[MdiStore] DrawIndirect unavailable — use per-batch draws";
  return false;
#endif
}

bool UMdiVertexPoolStore::TrySubmitMultiDraw(const GreedyGpuPassCache &cache)
{
  std::vector<DrawElementsIndirectCommand> cmds;
  if (BuildIndirectCommands(cache, cmds) == 0)
  {
    return false;
  }
  return SubmitIndirectCommands(cmds);
}

void UMdiVertexPoolStore::RefreshPassRefs(
    GreedyGpuPassCache &cache, const UChunkMeshCache &meshCache,
    const std::vector<GreedyBatchRef> &refs, uint64_t mesh_revision,
    uint64_t cull_revision, uint64_t sort_revision)
{
  // Live MapBucket path: stage concatenated vertex bytes then Unmap (mapped
  // VBO). Pool upload still goes through GreedyVertexPool mapped SubData.
  size_t stage_bytes = 0;
  for (const GreedyBatchRef &ref : refs)
  {
    if (const GreedyMeshBatch *b = meshCache.TryGetGreedyBatch(ref))
    {
      stage_bytes += b->vertices.size() * sizeof(GreedyMeshVertex);
    }
  }
  if (stage_bytes > 0)
  {
    MeshGpuBucketHandle handle{};
    handle.index = 0;
    handle.valid = true;
    void *mapped = MapBucket(handle, stage_bytes);
    if (mapped)
    {
      size_t offset = 0;
      for (const GreedyBatchRef &ref : refs)
      {
        const GreedyMeshBatch *b = meshCache.TryGetGreedyBatch(ref);
        if (!b || b->vertices.empty())
        {
          continue;
        }
        const size_t n = b->vertices.size() * sizeof(GreedyMeshVertex);
        std::memcpy(static_cast<uint8_t *>(mapped) + offset, b->vertices.data(),
                    n);
        offset += n;
      }
      UnmapBucket(handle);
      ++MappedUploadFrames;
    }
  }
  UCpuStagingGpuStore::RefreshPassRefs(cache, meshCache, refs, mesh_revision,
                                       cull_revision, sort_revision);
}

void *UMdiVertexPoolStore::MapBucket(MeshGpuBucketHandle handle, size_t bytes)
{
  MappedHandle = handle;
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (bytes == 0)
  {
    StagingScratch.clear();
    MappedPtr = nullptr;
    return nullptr;
  }
  if (MappedVbo == 0)
  {
    glGenBuffers(1, &MappedVbo);
  }
  glBindBuffer(GL_ARRAY_BUFFER, MappedVbo);
  if (bytes > MappedVboCapacity)
  {
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr,
                 GL_DYNAMIC_DRAW);
    MappedVboCapacity = bytes;
  }
  MappedPtr = glMapBufferRange(
      GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
      GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
  if (MappedPtr)
  {
    StagingScratch.clear();
    return MappedPtr;
  }
  // Fallback CPU scratch if map fails.
#endif
  StagingScratch.resize(bytes);
  MappedPtr = StagingScratch.empty() ? nullptr : StagingScratch.data();
  return MappedPtr;
}

void UMdiVertexPoolStore::UnmapBucket(MeshGpuBucketHandle handle)
{
  (void)handle;
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (MappedVbo != 0 && MappedPtr && StagingScratch.empty())
  {
    glBindBuffer(GL_ARRAY_BUFFER, MappedVbo);
    glUnmapBuffer(GL_ARRAY_BUFFER);
  }
  else if (MappedVbo != 0 && !StagingScratch.empty())
  {
    // Mapped failed earlier — upload scratch.
    glBindBuffer(GL_ARRAY_BUFFER, MappedVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(StagingScratch.size()),
                    StagingScratch.data());
  }
#endif
  MappedPtr = nullptr;
}

void UMdiVertexPoolStore::FlipBucketOwnership(MeshGpuBucketHandle handle)
{
  (void)handle;
  MappedHandle = {};
  StagingScratch.clear();
  MappedPtr = nullptr;
}

} // namespace cutum
