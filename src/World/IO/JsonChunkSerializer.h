#pragma once

#include "World/IO/IUChunkSerializer.h"
#include <cstddef>
#include <iosfwd>

namespace cutum
{

class UJsonChunkSerializer : public IUChunkSerializer
{
public:
  // The current writer uses at most three containers, machine numbers and
  // numeric block IDs. These read limits also allow legacy JSON extensions.
  static constexpr size_t kMaxContainerDepth = 16385;
  static constexpr size_t kMaxNumericTokenBytes = 4096;
  static constexpr size_t kMaxLegacyNameBytes = 64 * 1024;
  static size_t MaxFrameBytes() noexcept;

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
