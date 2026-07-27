#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/Backend/GpuHotPathFallback.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "glog/logging.h"
#include <cstring>
#include <vector>

namespace cutum
{
namespace
{

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
// Matches Frustum::IntersectsChunkAABB plane skips (near/top/bottom = 2,3,4).
const char *kCompactCullCompute = R"(#version 430
layout(local_size_x = 64) in;
struct DrawCmd {
  uint count;
  uint instanceCount;
  uint firstIndex;
  int baseVertex;
  uint baseInstance;
};
layout(std430, binding = 0) readonly buffer Spheres { vec4 spheres[]; };
layout(std430, binding = 1) buffer Cmds { DrawCmd cmds[]; };
layout(std430, binding = 2) writeonly buffer Vis { uint vis[]; };
layout(std140, binding = 3) uniform FrustumUBO {
  vec4 planes[6];
  vec4 camPosMaxDist; // xyz = camera, w = maxDist (0 = unlimited)
  uint batchCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

bool sphereInFrustum(vec3 c, float r) {
  for (int i = 0; i < 6; ++i) {
    if (i == 2 || i == 3 || i == 4) continue;
    if (dot(planes[i].xyz, c) + planes[i].w < -r) {
      return false;
    }
  }
  return true;
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= batchCount) {
    return;
  }
  vec4 a = spheres[i];
  vec3 c = a.xyz;
  float r = a.w;
  float maxDist = camPosMaxDist.w;
  bool inDist = true;
  if (maxDist > 0.0) {
    vec3 cam = camPosMaxDist.xyz;
    float d = length(c - cam);
    inDist = d <= maxDist + r;
  }
  const bool ok = sphereInFrustum(c, r) && inDist && cmds[i].count > 0u;
  cmds[i].instanceCount = ok ? 1u : 0u;
  vis[i] = ok ? 1u : 0u;
}
)";
#endif

} // namespace

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
  if (CullProgram)
  {
    glDeleteProgram(CullProgram);
    CullProgram = 0;
  }
  if (CullFrustumUbo)
  {
    glDeleteBuffers(1, &CullFrustumUbo);
    CullFrustumUbo = 0;
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
    cmd.instanceCount = gpu.drawInstanceCount > 0 ? gpu.drawInstanceCount : 0;
    if (cmd.instanceCount == 0)
    {
      continue;
    }
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

bool UMdiVertexPoolStore::SubmitIndirectCommandsGpuRange(
    const GreedyGpuPassCache &cache, size_t begin, size_t end)
{
  LastDrawCmds = 0;
#if !defined(GL_DRAW_INDIRECT_BUFFER) || defined(__ANDROID__) ||               \
    defined(CUBATARIUM_GLES)
  (void)cache;
  (void)begin;
  (void)end;
  return false;
#else
  if (!cache.GpuCompactActive || cache.IndirectCmdsBuffer == 0 ||
      begin >= end || end > cache.batches.size())
  {
    return false;
  }
  const GLsizei count = static_cast<GLsizei>(end - begin);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cache.IndirectCmdsBuffer);
#if defined(GL_ARB_multi_draw_indirect) || defined(glMultiDrawElementsIndirect)
  glMultiDrawElementsIndirect(
      GL_TRIANGLES, GL_UNSIGNED_INT,
      reinterpret_cast<void *>(begin * sizeof(DrawElementsIndirectCommand)),
      count, 0);
#else
  for (GLsizei i = 0; i < count; ++i)
  {
    glDrawElementsIndirect(
        GL_TRIANGLES, GL_UNSIGNED_INT,
        reinterpret_cast<void *>((begin + static_cast<size_t>(i)) *
                                 sizeof(DrawElementsIndirectCommand)));
  }
#endif
  LastDrawCmds = static_cast<uint64_t>(count);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  return true;
#endif
}

bool UMdiVertexPoolStore::TrySubmitMultiDraw(const GreedyGpuPassCache &cache)
{
  if (SubmitIndirectCommandsGpuRange(cache, 0, cache.batches.size()))
  {
    return true;
  }
  std::vector<DrawElementsIndirectCommand> cmds;
  if (BuildIndirectCommands(cache, cmds) == 0)
  {
    return false;
  }
  return SubmitIndirectCommands(cmds);
}

bool UMdiVertexPoolStore::EnsureCullProgram()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return false;
#else
  if (CullInitAttempted)
  {
    return CullProgram != 0;
  }
  CullInitAttempted = true;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &kCompactCullCompute, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[512];
    glGetShaderInfoLog(sh, 512, nullptr, log);
    LOG(WARNING) << "[MdiStore] compact cull compile failed: " << log;
    glDeleteShader(sh);
    return false;
  }
  CullProgram = glCreateProgram();
  glAttachShader(CullProgram, sh);
  glLinkProgram(CullProgram);
  glDeleteShader(sh);
  glGetProgramiv(CullProgram, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    LOG(WARNING) << "[MdiStore] compact cull link failed";
    glDeleteProgram(CullProgram);
    CullProgram = 0;
    return false;
  }
  glGenBuffers(1, &CullFrustumUbo);
  return true;
#endif
}

void UMdiVertexPoolStore::RebuildIndirectCmdTable(GreedyGpuPassCache &cache)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)cache;
  return;
#else
  const size_t n = cache.batches.size();
  if (n == 0 || !cache.usesVertexPool)
  {
    cache.GpuCompactActive = false;
    cache.IndirectCullReady = false;
    return;
  }
  std::vector<DrawElementsIndirectCommand> cmds(n);
  std::vector<float> spheres(n * 4);
  for (size_t i = 0; i < n; ++i)
  {
    const GreedyGpuBatch &b = cache.batches[i];
    DrawElementsIndirectCommand &cmd = cmds[i];
    if (!b.pooled || b.indexCountGl <= 0)
    {
      cmd = {};
    }
    else
    {
      cmd.count = static_cast<uint32_t>(b.indexCountGl);
      cmd.instanceCount = 1;
      cmd.firstIndex =
          static_cast<uint32_t>(b.eboByteOffset / sizeof(uint32_t));
      cmd.baseVertex =
          static_cast<int32_t>(b.vboByteOffset / sizeof(GreedyMeshVertex));
      cmd.baseInstance = 0;
    }
    spheres[i * 4 + 0] = b.cullSphere[0];
    spheres[i * 4 + 1] = b.cullSphere[1];
    spheres[i * 4 + 2] = b.cullSphere[2];
    spheres[i * 4 + 3] = b.cullSphere[3];
  }

  const size_t cmd_bytes = n * sizeof(DrawElementsIndirectCommand);
  const size_t sphere_bytes = n * 4 * sizeof(float);
  const size_t vis_bytes = n * sizeof(uint32_t);

  if (cache.IndirectCmdsBuffer == 0)
  {
    glGenBuffers(1, &cache.IndirectCmdsBuffer);
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cache.IndirectCmdsBuffer);
  if (cmd_bytes > cache.IndirectCmdCapacity)
  {
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(cmd_bytes),
                 cmds.data(), GL_DYNAMIC_DRAW);
    cache.IndirectCmdCapacity = cmd_bytes;
  }
  else
  {
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GLsizeiptr>(cmd_bytes), cmds.data());
  }

  if (cache.BatchSphereSsbo == 0)
  {
    glGenBuffers(1, &cache.BatchSphereSsbo);
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cache.BatchSphereSsbo);
  if (sphere_bytes > cache.BatchSphereCapacity)
  {
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(sphere_bytes), spheres.data(),
                 GL_DYNAMIC_DRAW);
    cache.BatchSphereCapacity = sphere_bytes;
  }
  else
  {
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GLsizeiptr>(sphere_bytes), spheres.data());
  }

  if (cache.CullVisSsbo == 0)
  {
    glGenBuffers(1, &cache.CullVisSsbo);
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cache.CullVisSsbo);
  if (vis_bytes > cache.CullVisCapacity)
  {
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(vis_bytes),
                 nullptr, GL_DYNAMIC_DRAW);
    cache.CullVisCapacity = vis_bytes;
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  cache.GpuCompactActive = true;
  cache.IndirectCullReady = false;
  cache.CompactVisCpuSynced = false;
#endif
}

void UMdiVertexPoolStore::RefreshPassRefs(
    GreedyGpuPassCache &cache, const UChunkMeshCache &meshCache,
    const std::vector<GreedyBatchRef> &refs, uint64_t mesh_revision,
    uint64_t cull_revision, uint64_t sort_revision)
{
  const bool geometry_refresh = !(cache.meshRevision == mesh_revision &&
                                  cache.sortRevision == sort_revision);
  // Single write path: parent RefreshPassRefs → GreedyVertexPool::Allocate
  // (glMapBufferRange). Do not stage a second MappedVbo copy (draw uses pool).
  UCpuStagingGpuStore::RefreshPassRefs(cache, meshCache, refs, mesh_revision,
                                       cull_revision, sort_revision);
  if (geometry_refresh && cache.usesVertexPool && !refs.empty())
  {
    ++MappedUploadFrames;
    RebuildIndirectCmdTable(cache);
  }
}

void UMdiVertexPoolStore::ApplyFrustumInstanceCull(
    GreedyGpuPassCache &cache, const Frustum &frustum,
    const glm::vec3 &camera_pos, float max_cull_distance)
{
  for (GreedyGpuBatch &b : cache.batches)
  {
    if (!b.pooled || b.indexCountGl <= 0)
    {
      b.drawInstanceCount = 0;
      continue;
    }
    const glm::vec3 center(b.cullSphere[0], b.cullSphere[1], b.cullSphere[2]);
    const float r = b.cullSphere[3];
    const glm::vec3 bmin = center - glm::vec3(r);
    const glm::vec3 bmax = center + glm::vec3(r);
    const bool vis =
        frustum.IntersectsChunkAABB(bmin, bmax, camera_pos, max_cull_distance,
                                    false);
    b.drawInstanceCount = vis ? 1u : 0u;
  }
  cache.IndirectCullReady = true;
  cache.GpuCompactActive = false;
  cache.CompactVisCpuSynced = true;
}

bool UMdiVertexPoolStore::SyncCompactVisToCpu(GreedyGpuPassCache &cache)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)cache;
  return false;
#else
  NoteGpuHotPathFallback();
  if (!cache.GpuCompactActive || cache.CompactVisCpuSynced ||
      cache.CullVisSsbo == 0 || cache.batches.empty())
  {
    return cache.CompactVisCpuSynced;
  }
  const size_t n = cache.batches.size();
  std::vector<uint32_t> vis(n, 0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, cache.CullVisSsbo);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(n * sizeof(uint32_t)), vis.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  for (size_t i = 0; i < n; ++i)
  {
    cache.batches[i].drawInstanceCount = vis[i] ? 1u : 0u;
  }
  cache.CompactVisCpuSynced = true;
  return true;
#endif
}

bool UMdiVertexPoolStore::ApplyGpuCompactCull(GreedyGpuPassCache &cache,
                                              const Frustum &frustum,
                                              const glm::vec3 &camera_pos,
                                              float max_cull_distance)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  ApplyFrustumInstanceCull(cache, frustum, camera_pos, max_cull_distance);
  return false;
#else
  if (!EnsureCullProgram() || cache.batches.empty())
  {
    if (!cache.batches.empty() && !cache.GpuCompactActive)
    {
      RebuildIndirectCmdTable(cache);
    }
    return cache.GpuCompactActive;
  }
  if (!cache.GpuCompactActive || cache.IndirectCmdsBuffer == 0 ||
      cache.BatchSphereSsbo == 0 || cache.CullVisSsbo == 0)
  {
    RebuildIndirectCmdTable(cache);
  }
  if (!cache.GpuCompactActive)
  {
    return false;
  }

  struct FrustumUboData
  {
    float planes[6][4];
    float camPosMaxDist[4];
    uint32_t batchCount;
    uint32_t pad[3];
  } ubo{};
  for (int i = 0; i < 6; ++i)
  {
    ubo.planes[i][0] = frustum.planes[static_cast<size_t>(i)].x;
    ubo.planes[i][1] = frustum.planes[static_cast<size_t>(i)].y;
    ubo.planes[i][2] = frustum.planes[static_cast<size_t>(i)].z;
    ubo.planes[i][3] = frustum.planes[static_cast<size_t>(i)].w;
  }
  ubo.camPosMaxDist[0] = camera_pos.x;
  ubo.camPosMaxDist[1] = camera_pos.y;
  ubo.camPosMaxDist[2] = camera_pos.z;
  ubo.camPosMaxDist[3] = max_cull_distance;
  ubo.batchCount = static_cast<uint32_t>(cache.batches.size());

  glBindBuffer(GL_UNIFORM_BUFFER, CullFrustumUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_UNIFORM_BUFFER, 3, CullFrustumUbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cache.BatchSphereSsbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cache.IndirectCmdsBuffer);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, cache.CullVisSsbo);
  glUseProgram(CullProgram);
  const uint32_t n = ubo.batchCount;
  glDispatchCompute((n + 63u) / 64u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
  glUseProgram(0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // No vis readback: IndirectCmdsBuffer is authoritative for MultiDraw.
  // Keep CPU drawInstanceCount=1 so rare DrawElementsBaseVertex fallback still
  // draws (overdraw-only if compact culled); avoids N-uint GetBufferSubData.
  for (GreedyGpuBatch &b : cache.batches)
  {
    b.drawInstanceCount =
        (b.pooled && b.indexCountGl > 0) ? 1u : 0u;
  }
  cache.IndirectCullReady = true;
  cache.GpuCompactActive = true;
  cache.CompactVisCpuSynced = false;
  return true;
#endif
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
