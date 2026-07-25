#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "glog/logging.h"

namespace cutum
{

UMdiVertexPoolStore::~UMdiVertexPoolStore()
{
  if (IndirectBuffer != 0)
  {
    glDeleteBuffers(1, &IndirectBuffer);
    IndirectBuffer = 0;
  }
}

size_t UMdiVertexPoolStore::BuildIndirectCommands(
    const GreedyGpuPassCache &cache,
    std::vector<DrawElementsIndirectCommand> &out)
{
  out.clear();
  if (!cache.usesVertexPool || cache.poolVbo == 0 || cache.poolEbo == 0)
  {
    return 0;
  }
  out.reserve(cache.batches.size());
  for (const GreedyGpuBatch &gpu : cache.batches)
  {
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

bool UMdiVertexPoolStore::TrySubmitMultiDraw(const GreedyGpuPassCache &cache)
{
  LastDrawCmds = 0;
  std::vector<DrawElementsIndirectCommand> cmds;
  if (BuildIndirectCommands(cache, cmds) == 0)
  {
    return false;
  }

#if defined(GL_DRAW_INDIRECT_BUFFER) && defined(GL_ARB_multi_draw_indirect)
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

  // MultiDraw requires same VAO attribs for all — caller must bind VAO/VBO/EBO.
  // Grouping by texture is caller's responsibility; here one MDI only when
  // all batches already share pool buffers (textures still per-draw without atlas).
  // Practical Phase 1: issue one DrawElementsIndirect per command (params from GPU).
  for (size_t i = 0; i < cmds.size(); ++i)
  {
    glDrawElementsIndirect(
        GL_TRIANGLES, GL_UNSIGNED_INT,
        reinterpret_cast<void *>(i * sizeof(DrawElementsIndirectCommand)));
  }
  LastDrawCmds = cmds.size();
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  return true;
#else
  (void)cache;
  LOG_FIRST_N(WARNING, 1)
      << "[MdiStore] DrawIndirect unavailable — use per-batch draws";
  return false;
#endif
}

void *UMdiVertexPoolStore::MapBucket(MeshGpuBucketHandle handle, size_t bytes)
{
  MappedHandle = handle;
  StagingScratch.resize(bytes);
  return StagingScratch.empty() ? nullptr : StagingScratch.data();
}

void UMdiVertexPoolStore::UnmapBucket(MeshGpuBucketHandle handle)
{
  (void)handle;
}

void UMdiVertexPoolStore::FlipBucketOwnership(MeshGpuBucketHandle handle)
{
  (void)handle;
  MappedHandle = {};
  StagingScratch.clear();
}

} // namespace cutum
