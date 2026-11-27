#pragma once

#include "World/IO/IUChunkSerializer.h"
#include <limits>

namespace cutum
{

class UBinaryChunkSerializer : public IUChunkSerializer
{
public:
  static constexpr char kMagic[4] = {'C', 'C', 'H', 'K'};
  /// v4: blocks + fluid + light + weather state. v3: adds light.
  /// v2: blocks + fluid. v1: blocks (+ liquid heuristic).
  static constexpr uint8_t kVersion = 4;
  static constexpr uint8_t kVersionLight = 3;
  static constexpr uint8_t kVersionFluid = 2;
  static constexpr uint8_t kVersionLegacy = 1;
  // Field widths on disk. Preserve the entire 16-bit palette range, including
  // unused entries in older worlds, and one positive-length run per cell.
  static constexpr size_t kMaxFrameBytes =
      sizeof(kMagic) + 1 + 3 * 4 + 2 +
      (std::numeric_limits<uint16_t>::max)() * 2 + 4 + CHUNK_VOLUME * 4 +
      2 * (4 + CHUNK_VOLUME) + 2 + 8;
  // The writer emits AIR and at most one palette entry per cell.
  static_assert(CHUNK_VOLUME < (std::numeric_limits<uint16_t>::max)());
  static constexpr size_t kMaxWrittenFrameBytes = kMaxFrameBytes -
      2 * ((std::numeric_limits<uint16_t>::max)() - (CHUNK_VOLUME + 1));

  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Binary; }
  const char *FileExtension() const override { return ".cchunk"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry,
                            const IoCodecProbe *probe = nullptr) const override;
  UChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                           glm::ivec3 chunkCoord,
                           UBlockRegistry &registry,
                           const IoCodecProbe *probe = nullptr) const override;
};

} // namespace cutum
