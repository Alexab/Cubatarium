#include "World/Chunks/ChunkGenerationToken.h"
#include <unordered_map>
#include <limits>
#include <stdexcept>

namespace cutum
{

ChunkGenerationToken UChunkGenerationRegistry::Current(glm::ivec3 coord) const
{
  std::lock_guard<std::mutex> lock(Mutex);
  ChunkGenerationToken token;
  token.coord = coord;
  const auto it = Sequences.find(coord);
  token.sequence = it == Sequences.end() ? 0 : it->second;
  return token;
}

uint64_t UChunkGenerationRegistry::Bump(glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(Mutex);
  if (NextSequence == std::numeric_limits<uint64_t>::max())
    throw std::overflow_error("Generation token sequence exhausted");
  return Sequences[coord] = ++NextSequence;
}

ChunkGenerationToken UChunkGenerationRegistry::EnsureCurrent(glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(Mutex);
  auto existing = Sequences.find(coord);
  if (existing != Sequences.end()) return {coord,existing->second};
  if (NextSequence == std::numeric_limits<uint64_t>::max())
    throw std::overflow_error("Generation token sequence exhausted");
  return {coord,Sequences[coord] = ++NextSequence};
}

bool UChunkGenerationRegistry::Retire(glm::ivec3 coord, uint64_t expected_sequence)
{
  std::lock_guard<std::mutex> lock(Mutex);
  const auto current = Sequences.find(coord);
  if (current == Sequences.end() || current->second != expected_sequence) return false;
  Sequences.erase(current);
  return true;
}

size_t UChunkGenerationRegistry::LiveTokenCount() const
{
  std::lock_guard<std::mutex> lock(Mutex);
  return Sequences.size();
}

} // namespace cutum
