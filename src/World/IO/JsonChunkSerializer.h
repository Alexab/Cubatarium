#pragma once

#include "World/IO/IUChunkSerializer.h"

namespace cutum
{

class UJsonChunkSerializer : public IUChunkSerializer
{
public:
  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Json; }
  const char *FileExtension() const override { return ".json"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry) const override;
  UChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                           glm::ivec3 chunkCoord,
                           UBlockRegistry &registry) const override;
};

} // namespace cutum
