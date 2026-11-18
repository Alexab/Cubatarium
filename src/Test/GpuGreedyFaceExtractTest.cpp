#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

size_t Popcount32(uint32_t m)
{
  size_t n = 0;
  while (m != 0)
  {
    n += m & 1u;
    m >>= 1;
  }
  return n;
}

} // namespace

int main()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  constexpr BlockId kStone = 8;
  BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  std::unordered_map<BlockId, BlockDefinition> by_id;
  by_id[kStone] = stone;
  std::unordered_map<std::string, BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  UBlockRegistry registry(nullptr, definitions);

  ChunkMeshSnapshot snap;
  snap.coord = glm::ivec3(0, 0, 0);
  snap.blocks.fill(0);
  // 2x2x2 solid cube in corner — exterior faces = 24.
  for (int y = 0; y < 2; ++y)
  {
    for (int z = 0; z < 2; ++z)
    {
      for (int x = 0; x < 2; ++x)
      {
        const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
        snap.blocks[static_cast<size_t>(li)] = kStone;
      }
    }
  }
  Expect(SnapshotIsGpuExtractEligible(snap, registry), "eligible opaque");
  const auto faces = ExtractOpaqueFacesCpu(snap, registry);
  Expect(faces.size() == 24, "2x2x2 exterior faces == 24");

  std::vector<uint32_t> visibility;
  BuildPaddedFaceVisibility(snap, registry, nullptr, visibility);
  Expect((visibility[FaceVisibilityIndex({0, 0, 0})] & kFaceSolid) != 0,
         "origin has solid material semantics");
  std::vector<uint32_t> masks(CHUNK_VOLUME, 0);
  for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        if (!(visibility[FaceVisibilityIndex(local)] & kFaceEligible))
          continue;
        for (int axis = 0; axis < 3; ++axis)
          for (int sign : {-1, 1})
          {
            glm::ivec3 direction(0);
            direction[axis] = sign;
            if (CubeFaceVisible(visibility, local, direction))
              masks[x + CHUNK_SIZE * z + CHUNK_SIZE * CHUNK_SIZE * y] |=
                  1u << (axis * 2 + (sign > 0 ? 1 : 0));
          }
      }
  size_t mask_faces = 0;
  for (uint32_t m : masks)
  {
    mask_faces += Popcount32(m);
  }
  Expect(mask_faces == faces.size(), "mask popcount == extract faces");

  const auto merged = MergeOpaqueQuadsStrict(faces);
  Expect(merged.size() < faces.size(), "strict merge reduces quad count");
  size_t merged_area = 0;
  for (const GreedyQuad &q : merged)
  {
    Expect(q.width >= 1 && q.height >= 1, "merged dims valid");
    merged_area += static_cast<size_t>(q.width * q.height);
  }
  Expect(merged_area == faces.size(), "merged area covers all 1x1 faces");

  // Parity: merged quad count is stable for uniform 2x2x2 stone.
  Expect(merged.size() <= faces.size(), "merge does not grow quad count");

  if (gFails != 0)
  {
    std::cerr << "gpu_greedy_face_extract_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "gpu_greedy_face_extract_test: ok faces=" << faces.size()
            << " merged=" << merged.size() << "\n";
  return 0;
}
