#include "World/Chunks/ChunkLoadPriority.h"

#include "World/Chunks/Chunk.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

int ChunkChebyshevDistance(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk)
{
  chunk_ground.y = 0;
  feet_chunk.y = 0;
  return std::max(std::abs(chunk_ground.x - feet_chunk.x),
                  std::abs(chunk_ground.z - feet_chunk.z));
}

bool IsFeetNeighborhood(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk)
{
  return ChunkChebyshevDistance(chunk_ground, feet_chunk) <= 1;
}

int ComputeChunkLoadPriority(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk,
                             glm::vec3 view_forward_xz,
                             const ChunkLoadPriorityParams &params)
{
  const int chebyshev = ChunkChebyshevDistance(chunk_ground, feet_chunk);
  int priority = chebyshev * params.ChebyshevScale;

  view_forward_xz.y = 0.0f;
  if (glm::length(view_forward_xz) > 0.01f)
  {
    const glm::vec3 forward = glm::normalize(view_forward_xz);
    const float chunk_center_x =
        static_cast<float>(chunk_ground.x) * static_cast<float>(CHUNK_SIZE) +
        static_cast<float>(CHUNK_SIZE) * 0.5f;
    const float chunk_center_z =
        static_cast<float>(chunk_ground.z) * static_cast<float>(CHUNK_SIZE) +
        static_cast<float>(CHUNK_SIZE) * 0.5f;
    const float feet_x =
        static_cast<float>(feet_chunk.x) * static_cast<float>(CHUNK_SIZE) +
        static_cast<float>(CHUNK_SIZE) * 0.5f;
    const float feet_z =
        static_cast<float>(feet_chunk.z) * static_cast<float>(CHUNK_SIZE) +
        static_cast<float>(CHUNK_SIZE) * 0.5f;
    glm::vec3 chunk_dir(chunk_center_x - feet_x, 0.0f, chunk_center_z - feet_z);
    if (glm::length(chunk_dir) > 0.01f)
    {
      chunk_dir = glm::normalize(chunk_dir);
      const float view_dot = glm::dot(chunk_dir, forward);
      priority -= static_cast<int>(std::max(0.0f, view_dot) *
                                   static_cast<float>(params.ViewBiasWeight));
      if (view_dot > 0.7f && !IsFeetNeighborhood(chunk_ground, feet_chunk))
      {
        priority -= params.ViewAheadBonus;
      }
    }
  }

  if (IsFeetNeighborhood(chunk_ground, feet_chunk))
  {
    priority -= params.FeetRingBonus;
  }
  return priority;
}

} // namespace cutum
