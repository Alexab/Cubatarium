#pragma once

#include "World/IO/IChunkSerializer.h"

namespace cutum
{

class BinaryChunkSerializer : public IChunkSerializer
{
public:
  static constexpr char kMagic[4] = {'C', 'C', 'H', 'K'};
  static constexpr uint8_t kVersion = 1;

  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Binary; }
  const char *FileExtension() const override { return ".cchunk"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry) const override;
  ChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                          glm::ivec3 chunkCoord,
                          UBlockRegistry &registry) const override;
};

} // namespace cutum
