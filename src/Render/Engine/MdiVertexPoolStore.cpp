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
