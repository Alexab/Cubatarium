from pathlib import Path

p = Path("src/Render/Mesh/ChunkMeshCache.cpp")
t = p.read_text(encoding="utf-8")
old = """void UChunkMeshCache::AppendGreedyPassBatchRefs(
    glm::ivec3 coord, bool transparent_pass,
    std::vector<GreedyBatchRef> &out) const
{
  const auto it = GreedyCache.find(coord);
  if (it == GreedyCache.end())
    return;
  const std::vector<GreedyMeshBatch> &batches = it->second.batches;
  for (size_t i = 0; i < batches.size(); ++i)
  {
    const GreedyMeshBatch &batch = batches[i];
    if (batch.vertices.empty() || batch.indices.empty())
      continue;
    if (batch.Transparent != transparent_pass)
      continue;
    out.push_back(GreedyBatchRef{coord, static_cast<uint16_t>(i), batch.blockId});
  }
}"""
new = """void UChunkMeshCache::AppendGreedyPassBatchRefs(
    glm::ivec3 coord, bool transparent_pass,
    std::vector<GreedyBatchRef> &out) const
{
  const auto it = GreedyCache.find(coord);
  if (it == GreedyCache.end())
    return;
  AppendGreedyPassBatchRefsFromBatches(coord, transparent_pass,
                                       it->second.batches, out);
}"""
if old not in t:
    raise SystemExit("old Append not found")
t = t.replace(old, new, 1)
inc = '#include "Render/Engine/GreedyPassBatchRefs.h"\n'
if "GreedyPassBatchRefs.h" not in t:
    t = t.replace(
        '#include "Render/Mesh/ChunkMeshCache.h"\n',
        '#include "Render/Mesh/ChunkMeshCache.h"\n' + inc,
        1,
    )
p.write_text(t, encoding="utf-8")
print("ChunkMeshCache Append updated")

# Live Append hook for production tests (same algorithm, test-local map).
tp = Path("src/Test/GreedyVertexPoolProductionTest.cpp")
tt = tp.read_text(encoding="utf-8")
old_stub = """// Production test binary does not link ChunkMeshCache.cpp; PublishPassInputs
// only calls Append when mesh_cache != nullptr (RefreshPassRefs path).
namespace cutum
{
void UChunkMeshCache::AppendGreedyPassBatchRefs(
    glm::ivec3, bool, std::vector<GreedyBatchRef> &) const
{
}
} // namespace cutum
"""
new_stub = """// Production test binary does not link ChunkMeshCache.cpp. Provide the same
// Append algorithm via a test-local GreedyCache map (audit S2 live Append).
#include "Render/Engine/GreedyPassBatchRefs.h"
#include "World/Math/BlockTypes.h"
namespace cutum
{
namespace
{
std::unordered_map<glm::ivec3, std::vector<GreedyMeshBatch>, IVec3Hash>
    g_test_greedy_batches;
}

void SetTestGreedyPassBatches(
    glm::ivec3 coord, const std::vector<GreedyMeshBatch> &batches)
{
  g_test_greedy_batches[coord] = batches;
}

void ClearTestGreedyPassBatches() { g_test_greedy_batches.clear(); }

void UChunkMeshCache::AppendGreedyPassBatchRefs(
    glm::ivec3 coord, bool transparent_pass,
    std::vector<GreedyBatchRef> &out) const
{
  const auto it = g_test_greedy_batches.find(coord);
  if (it == g_test_greedy_batches.end())
    return;
  AppendGreedyPassBatchRefsFromBatches(coord, transparent_pass, it->second,
                                       out);
}
} // namespace cutum
"""
if old_stub not in tt:
    raise SystemExit("old stub not found")
tt = tt.replace(old_stub, new_stub, 1)
tp.write_text(tt, encoding="utf-8")
print("test Append live hook updated")
