#pragma once

#include "World/IO/IUChunkSerializer.h"

namespace cutum
{

class UBinaryChunkSerializer : public IUChunkSerializer
{
public:
  static constexpr char kMagic[4] = {'C', 'C', 'H', 'K'};
  /// v3: blocks + fluid + light. v2: blocks + fluid. v1: blocks (+ liquid heuristic).
  static constexpr uint8_t kVersion = 3;
  static constexpr uint8_t kVersionFluid = 2;
  static constexpr uint8_t kVersionLegacy = 1;

  ChunkDiskFormat GetFormat() const override { return ChunkDiskFormat::Binary; }
  const char *FileExtension() const override { return ".cchunk"; }
  SerializedChunk Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                            UBlockRegistry &registry) const override;
  UChunkBuffer Deserialize(const std::vector<uint8_t> &bytes,
                           glm::ivec3 chunkCoord,
                           UBlockRegistry &registry) const override;
};

} // namespace cutum
