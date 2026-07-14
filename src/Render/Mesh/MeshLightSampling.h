#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightUtil.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace cutum
{

inline float LightLevel01(int level)
{
  return static_cast<float>(std::clamp(level, 0, kMaxLightLevel)) /
         static_cast<float>(kMaxLightLevel);
}

inline void ApplyVertexLight(GreedyMeshVertex &vertex, uint8_t packed_light)
{
  vertex.skyLight = LightLevel01(UnpackSky(packed_light));
  vertex.blockLight = LightLevel01(UnpackBlock(packed_light));
}

inline uint8_t SampleLightPacked(const ChunkMeshSnapshot &snapshot,
                                 glm::ivec3 world_voxel)
{
  const glm::ivec3 local = world_voxel - snapshot.ChunkOrigin();
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return 0;
  }
  return snapshot.GetLightPackedLocal(local);
}

inline uint8_t SampleLightPacked(const UBlockWorld &world,
                                 glm::ivec3 world_voxel)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_voxel);
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return 0;
  }
  return chunk->GetLightPackedLocal(UChunkManager::WorldToLocal(world_voxel));
}

inline void ApplyVertexLight(GreedyMeshVertex &vertex,
                             const ChunkMeshSnapshot &snapshot,
                             glm::ivec3 world_voxel)
{
  ApplyVertexLight(vertex, SampleLightPacked(snapshot, world_voxel));
}

inline void ApplyVertexLight(GreedyMeshVertex &vertex, const UBlockWorld &world,
                             glm::ivec3 world_voxel)
{
  ApplyVertexLight(vertex, SampleLightPacked(world, world_voxel));
}

struct CrossInstanceLight
{
  float skyLight{0.f};
  float blockLight{0.f};
};

inline CrossInstanceLight SampleCrossInstanceLight(const UChunk &chunk,
                                                   glm::ivec3 world_voxel)
{
  const glm::ivec3 local = UChunkManager::WorldToLocal(world_voxel);
  const uint8_t packed = chunk.GetLightPackedLocal(local);
  return {LightLevel01(UnpackSky(packed)),
          LightLevel01(UnpackBlock(packed))};
}

inline CrossInstanceLight
SampleCrossInstanceLight(const ChunkMeshSnapshot &snapshot,
                         glm::ivec3 world_voxel)
{
  return {LightLevel01(UnpackSky(SampleLightPacked(snapshot, world_voxel))),
          LightLevel01(UnpackBlock(SampleLightPacked(snapshot, world_voxel)))};
}

} // namespace cutum
