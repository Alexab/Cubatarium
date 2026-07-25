#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

struct DrawElementsIndirectCommand
{
  uint32_t count{0};
  uint32_t instanceCount{1};
  uint32_t firstIndex{0};
  int32_t baseVertex{0};
  uint32_t baseInstance{0};
};

struct FakeBatch
{
  bool pooled{false};
  int indexCountGl{0};
  size_t eboByteOffset{0};
  size_t vboByteOffset{0};
};

struct FakeCache
{
  bool usesVertexPool{false};
  unsigned poolVbo{0};
  unsigned poolEbo{0};
  std::vector<FakeBatch> batches;
};

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

size_t BuildRange(const FakeCache &cache, size_t begin, size_t end,
                  std::vector<DrawElementsIndirectCommand> &out)
{
  out.clear();
  if (!cache.usesVertexPool || cache.poolVbo == 0 || cache.poolEbo == 0)
  {
    return 0;
  }
  constexpr size_t kVert = 32; // fake sizeof vertex
  for (size_t i = begin; i < end && i < cache.batches.size(); ++i)
  {
    const FakeBatch &gpu = cache.batches[i];
    if (!gpu.pooled || gpu.indexCountGl <= 0)
    {
      continue;
    }
    DrawElementsIndirectCommand cmd;
    cmd.count = static_cast<uint32_t>(gpu.indexCountGl);
    cmd.firstIndex =
        static_cast<uint32_t>(gpu.eboByteOffset / sizeof(uint32_t));
    cmd.baseVertex = static_cast<int32_t>(gpu.vboByteOffset / kVert);
    out.push_back(cmd);
  }
  return out.size();
}

} // namespace

int main()
{
  FakeCache cache;
  cache.usesVertexPool = true;
  cache.poolVbo = 1;
  cache.poolEbo = 1;
  FakeBatch b;
  b.pooled = true;
  b.indexCountGl = 6;
  b.eboByteOffset = 0;
  b.vboByteOffset = 0;
  cache.batches.push_back(b);
  b.eboByteOffset = 24;
  b.vboByteOffset = 64; // 2 verts * 32
  cache.batches.push_back(b);

  std::vector<DrawElementsIndirectCommand> cmds;
  Expect(BuildRange(cache, 0, 2, cmds) == 2, "two cmds");
  Expect(cmds[0].count == 6 && cmds[0].baseVertex == 0, "cmd0");
  Expect(cmds[1].firstIndex == 6 && cmds[1].baseVertex == 2, "cmd1 baseVertex");
  Expect(BuildRange(cache, 1, 2, cmds) == 1, "range single");
  Expect(cmds[0].baseVertex == 2, "range baseVertex");

  if (gFails != 0)
  {
    std::cerr << "mesh_gpu_store_mdi_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "mesh_gpu_store_mdi_test: ok\n";
  return 0;
}
