#include "World/Chunks/ChunkGenerationToken.h"
#include <unordered_map>

namespace cutum
{

ChunkGenerationToken UChunkGenerationRegistry::Current(glm::ivec3 coord) const
{
  ChunkGenerationToken token;
  token.coord = coord;
  const auto it = Sequences.find(coord);
  token.sequence = it == Sequences.end() ? 0 : it->second;
  return token;
}

uint64_t UChunkGenerationRegistry::Bump(glm::ivec3 coord)
{
  return ++Sequences[coord];
}

} // namespace cutum
