#pragma once

#include <glm/glm.hpp>

namespace cutum
{

struct ChunkLoadPriorityParams
{
  int ChebyshevScale{100};
  int ViewBiasWeight{24};
  int FeetRingBonus{1000};
};

int ChunkChebyshevDistance(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk);
bool IsFeetNeighborhood(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk);
int ComputeChunkLoadPriority(glm::ivec3 chunk_ground, glm::ivec3 feet_chunk,
                             glm::vec3 view_forward_xz,
                             const ChunkLoadPriorityParams &params);

} // namespace cutum
