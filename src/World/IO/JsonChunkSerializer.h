#pragma once

#include "World/IO/IChunkSerializer.h"

namespace cutum
{

class JsonChunkSerializer : public IChunkSerializer
{
public:
  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Json; }
  const char *FileExtension() const override { return ".json"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry) const override;
  ChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                          glm::ivec3 chunkCoord,
                          UBlockRegistry &registry) const override;
};

} // namespace cutum
