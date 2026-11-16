#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Diagnostics/JobStageTrace.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuFullLightingPipeline.h"
#include "World/Lighting/LightUtil.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace cutum
{
void SetTestSkylightSeedAvailable(bool available);
uint64_t UJobStageTrace::GetStreamingFrameEpoch() { return 0; }
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_lighting_propagation_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockRegistry> MakeRegistry()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = kStone;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition torch;
  torch.Name = "torch";
  torch.Id = kTorch;
  torch.Physics = cutum::BlockPhysicsProfile::Solid();
  torch.Render.Style = cutum::BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Id = 9;
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kTorch] = torch;
  by_id[9] = water;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["torch"] = kTorch;
  name_to_id["water"] = 9;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return std::make_shared<cutum::UBlockRegistry>(nullptr, definitions);
}

static int SkyAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetSkyLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static int BlockAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetBlockLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static void BuildEnclosedRoom(cutum::UBlockWorld &world, cutum::BlockId stone,
                              int floor_y)
{
  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, floor_y, z), stone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), stone);
      world.SetBlock(glm::ivec3(x, y, 11), stone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), stone);
      world.SetBlock(glm::ivec3(11, y, z), stone);
    }
  }
}

int main()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  constexpr int kMaxWorldY = cutum::CHUNK_SIZE - 1;
  auto registry = MakeRegistry();
  cutum::UBlockWorld world;
  const glm::ivec3 chunk_coord(0, 0, 0);

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 15, 8)) == cutum::kMaxLightLevel,
         "open sky at chunk top");

  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, 8, z), kStone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), kStone);
      world.SetBlock(glm::ivec3(x, y, 11), kStone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), kStone);
      world.SetBlock(glm::ivec3(11, y, z), kStone);
    }
  }

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 5, 8)) == 0, "enclosed room skylight");

  world.SetBlock(glm::ivec3(8, 5, 8), kTorch);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 8)) >= 13, "torch emission");
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) > 0, "torch blocklight spread");

  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) == 0,
         "torch blocklight removed after break");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 1; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0, "skylight enters open shaft");
  world.SetBlock(glm::ivec3(8, 2, 8), kStone);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) == 0,
         "skylight blocked by second shaft block");
  world.SetBlock(glm::ivec3(8, 2, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0,
         "skylight restored after shaft reopen");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  BuildEnclosedRoom(world, kStone, 5);
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) == 0,
         "deep room dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) > 0,
         "deep shaft to surface relights room");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), kStone);
  for (int x = 8; x <= 17; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), kStone);
    world.SetBlock(glm::ivec3(x, 6, 8), kStone);
    world.SetBlock(glm::ivec3(x, 4, 8), kStone);
    world.SetBlock(glm::ivec3(x, 5, 7), kStone);
    world.SetBlock(glm::ivec3(x, 5, 9), kStone);
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 4, z), kStone);
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      for (int y = 6; y <= 8; ++y)
      {
        if (x == 11 || x == 13 || z == 7 || z == 9)
        {
          world.SetBlock(glm::ivec3(x, y, z), kStone);
        }
      }
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) == 0,
         "offset cave dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  for (int x = 9; x <= 16; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) > 0,
         "offset cave lit after shaft and tunnel open");

  {
    cutum::UBlockWorld neigh_world;
    for (int x = 0; x < 32; ++x)
    {
      for (int z = 0; z < 16; ++z)
      {
        neigh_world.SetBlock(glm::ivec3(x, 0, z), kStone);
      }
    }
    neigh_world.SetBlock(glm::ivec3(20, 4, 8), kTorch);
    cutum::RelightChunk(neigh_world, *registry, glm::ivec3(0, 0, 0));
    cutum::RelightChunk(neigh_world, *registry, glm::ivec3(1, 0, 0));
    cutum::RelightJobSpec spec;
    spec.block_positions = {glm::ivec3(8, 0, 8)};
    spec.min_world_y = 0;
    spec.max_world_y = 15;
    spec.column_center_only = true;
    spec.frontier_iterations = 0;
    auto snap = cutum::UChunkRelightSnapshot::Capture(neigh_world, spec);
    Expect(snap.GetCapturedFullChunks() == 1,
           "D: center-only copies one full chunk");
    Expect(snap.HasChunk(glm::ivec3(0, 0, 0)),
           "D: center chunk has blocks");
    Expect(!snap.HasChunk(glm::ivec3(1, 0, 0)),
           "D: neighbor not in Blocks");
    Expect(snap.HasLight(glm::ivec3(1, 0, 0)),
           "D: neighbor light copied as seed");
    Expect(snap.GetCapturedNeighborLightChunks() >= 1,
           "D: neighbor light count >= 1");
    auto result = snap.Compute(*registry);
    Expect(result.chunks.size() == 1,
           "D: apply only center light");
    bool center_edge_lit = false;
    for (const auto &chunk_data : result.chunks)
    {
      if (chunk_data.coord != glm::ivec3(0, 0, 0))
      {
        continue;
      }
      const glm::ivec3 local(15, 4, 8);
      const int idx = local.x + cutum::CHUNK_SIZE *
                                    (local.y + cutum::CHUNK_SIZE * local.z);
      const int block_light = (chunk_data.light_packed[idx] >> 4) & 0x0F;
      center_edge_lit = block_light > 0;
    }
    Expect(center_edge_lit, "D: neighbor torch seeds center face");
  }

  {
    constexpr int kTallWorldMaxY = 319;
    cutum::UBlockWorld tall_world;
    const glm::ivec3 torch_pos(8, kTallWorldMaxY - 1, 8);
    tall_world.SetBlock(torch_pos, kTorch);
    cutum::RelightBlocksAroundAll(tall_world, *registry, {torch_pos},
                                  kTallWorldMaxY);
    Expect(BlockAt(tall_world, torch_pos + glm::ivec3(0, 1, 0)) > 0,
           "block light propagates inside the top supported chunk slice");
  }

  {
    constexpr int kMinWorldY = -64;
    cutum::UBlockWorld deep_world;
    deep_world.GetChunkManager().EnsureChunk(glm::ivec3(0, -4, 0));
    deep_world.GetChunkManager().EnsureChunk(glm::ivec3(0, -3, 0));
    const glm::ivec3 torch_pos(8, -49, 8);
    deep_world.SetBlock(torch_pos, kTorch);
    cutum::RelightColumn(deep_world, *registry, torch_pos.x, torch_pos.z,
                         kMinWorldY, -33, true, false);
    Expect(BlockAt(deep_world, torch_pos) >= 13,
           "block light source is processed below world Y=0");
    Expect(BlockAt(deep_world, glm::ivec3(8, -48, 8)) > 0,
           "block light crosses the negative chunk seam at Y=-48");
  }

  // E: one union flood per batch must reach the same fixed point the
  // per-chunk floods reached. Three stacked chunks, two torches in separate
  // air shafts; assert exact max-plus attenuation across both chunk seams.
  {
    cutum::UBlockWorld stack_world;
    for (int y = 0; y < 48; ++y)
    {
      for (int x = 0; x < 16; ++x)
      {
        for (int z = 0; z < 16; ++z)
        {
          stack_world.SetBlock(glm::ivec3(x, y, z), kStone);
        }
      }
    }
    for (int y = 0; y < 48; ++y)
    {
      stack_world.SetBlock(glm::ivec3(4, y, 4), cutum::BLOCK_AIR);
      stack_world.SetBlock(glm::ivec3(12, y, 12), cutum::BLOCK_AIR);
    }
    const glm::ivec3 torch_a(4, 6, 4);
    const glm::ivec3 torch_b(12, 26, 12);
    stack_world.SetBlock(torch_a, kTorch);
    stack_world.SetBlock(torch_b, kTorch);

    cutum::RelightJobSpec spec;
    spec.block_positions = {glm::ivec3(8, 0, 8)};
    spec.min_world_y = 0;
    spec.max_world_y = 47;
    spec.column_center_only = true;
    spec.frontier_iterations = 0;
    auto snap = cutum::UChunkRelightSnapshot::Capture(stack_world, spec);
    Expect(snap.GetCapturedFullChunks() == 3,
           "E: center-only copies the three-band stack");
    auto result = snap.Compute(*registry);

    auto block_light_at = [&result](glm::ivec3 pos)
    {
      const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
      for (const auto &chunk_data : result.chunks)
      {
        if (chunk_data.coord != chunk_coord)
        {
          continue;
        }
        const glm::ivec3 local = cutum::UChunkManager::WorldToLocal(pos);
        const int idx = local.x + cutum::CHUNK_SIZE *
                                       (local.y + cutum::CHUNK_SIZE * local.z);
        return (chunk_data.light_packed[idx] >> 4) & 0x0F;
      }
      return -1;
    };

    // Torch A at (4,6,4), emission 14, straight air shaft up: 14 - dy.
    Expect(block_light_at(glm::ivec3(4, 15, 4)) == 5,
           "E: torch A lights bottom-chunk seam face");
    Expect(block_light_at(glm::ivec3(4, 16, 4)) == 4,
           "E: torch A crosses seam into middle chunk");
    Expect(block_light_at(glm::ivec3(4, 19, 4)) == 1,
           "E: torch A reaches attenuation tail in middle chunk");
    Expect(block_light_at(glm::ivec3(4, 20, 4)) == 0,
           "E: torch A exhausts at range");
    // Torch B at (12,26,12): crosses the middle→top seam.
    Expect(block_light_at(glm::ivec3(12, 31, 12)) == 9,
           "E: torch B lights middle-chunk seam face");
    Expect(block_light_at(glm::ivec3(12, 32, 12)) == 8,
           "E: torch B crosses seam into top chunk");
    // Both shafts converge in the shared flood without starving either seed.
    Expect(block_light_at(torch_a) == 14, "E: torch A keeps source emission");
    Expect(block_light_at(torch_b) == 14, "E: torch B keeps source emission");
  }

  // A partial relight at negative Y must use the entire vertical sky context,
  // including attenuation in read-only water above its writable slice.
  cutum::SetTestSkylightSeedAvailable(true);
  for (const bool roof : {true, false})
  {
    cutum::UBlockWorld deep;
    for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
      for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
      {
        deep.SetBlock({x, -48, z}, kStone);
        if (roof)
          deep.SetBlock({x, 304, z}, kStone);
        else
          for (int y = -32; y <= -30; ++y)
            deep.SetBlock({x, y, z}, 9);
      }
    const int expected = roof ? 0 : 12;
    cutum::RelightChunk(deep, *registry, {0, -3, 0});
    Expect(SkyAt(deep, {8, -33, 8}) == expected,
           roof ? "sync sky sees roof more than 240 blocks above negative slice"
                : "sync sky retains attenuation across water slice boundary");
    cutum::UGpuFullLightingPipeline desktop_pipeline;
    desktop_pipeline.RelightChunk(deep, *registry, {0, -3, 0});
    Expect(SkyAt(deep, {8, -33, 8}) == expected,
           roof
               ? "desktop pipeline retains full-height roof with seed available"
               : "desktop pipeline retains incoming water attenuation");
    cutum::RelightJobSpec spec;
    spec.block_positions.push_back({8, -48, 8});
    spec.min_world_y = -48;
    spec.max_world_y = -33;
    spec.column_center_only = true;
    spec.include_block_light = false;
    auto snapshot = cutum::UChunkRelightSnapshot::Capture(deep, spec);
    const auto result = snapshot.Compute(*registry);
    Expect(result.chunks.size() == 1,
           "read-only full-height sky context does not expand writable band");
    const int idx = cutum::UChunk::LocalIndex({8, 15, 8});
    Expect((result.chunks.front().light_packed[idx] & 0x0f) == expected,
           roof ? "async sky sees full-height opaque roof"
                : "async sky retains cross-slice liquid attenuation");
    if (roof)
    {
      bool roof_recorded = false;
      for (const auto &stamp : result.read_set)
        roof_recorded |= stamp.coord == glm::ivec3(0, 19, 0) &&
                         stamp.readsContent && !stamp.readsLight;
      Expect(roof_recorded,
             "far roof geometry participates in async result validation");
    }
  }
  cutum::SetTestSkylightSeedAvailable(false);

  std::cout << "chunk_lighting_propagation_test: OK" << std::endl;
  return 0;
}
