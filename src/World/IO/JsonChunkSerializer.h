#pragma once

#include "World/IO/IUChunkSerializer.h"
#include <iosfwd>

namespace cutum
{

class UJsonChunkSerializer : public IUChunkSerializer
{
public:
  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Json; }
  const char *FileExtension() const override { return ".json"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry,
                            const IoCodecProbe *probe = nullptr) const override;
  UChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                           glm::ivec3 chunkCoord,
                           UBlockRegistry &registry,
                           const IoCodecProbe *probe = nullptr) const override;
  UChunkBuffer DeserializeStream(std::istream &file, glm::ivec3 chunkCoord,
                                 UBlockRegistry &registry, const IoCodecProbe *probe = nullptr,
                                 bool withholdFrame = false) const;
};

} // namespace cutum
