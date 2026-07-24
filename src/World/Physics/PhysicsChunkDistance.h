#ifndef PHYSICSCHUNKDISTANCE_H
#define PHYSICSCHUNKDISTANCE_H

#include <glm/glm.hpp>

namespace cutum
{

int ChebyshevChunkDistance(glm::ivec3 a, glm::ivec3 b);
int ChebyshevBlockDistanceChunks(glm::ivec3 block_pos, glm::ivec3 focus_chunk);

} // namespace cutum

#endif // PHYSICSCHUNKDISTANCE_H
