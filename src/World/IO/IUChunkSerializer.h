#pragma once

#include "World/Chunks/ChunkBuffer.h"
#include "World/IO/ChunkStorageTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;
class UChunk;

class IUChunkSerializer
{
public:
  virtual ~IUChunkSerializer() = default;

  virtual ChunkDiskFormat GetFormat() const = 0;
  virtual const char *FileExtension() const = 0;
  virtual SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                                    UBlockRegistry &registry) const = 0;
  virtual UChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                                   glm::ivec3 chunkCoord,
                                   UBlockRegistry &registry) const = 0;
};

} // namespace cutum
