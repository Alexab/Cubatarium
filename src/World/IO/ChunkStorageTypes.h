#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cutum
{

enum class ChunkDiskFormat
{
  Absent,
  Binary,
  Json,
};

enum class ChunkWriteFormat
{
  Binary,
  Json,
};

struct ChunkStorageSettings
{
  ChunkWriteFormat writeFormat{ChunkWriteFormat::Binary};
  bool deleteLegacyJsonOnBinarySave{true};
};

struct SerializedChunk
{
  std::vector<uint8_t> bytes;
  ChunkDiskFormat format{ChunkDiskFormat::Binary};
};

ChunkWriteFormat ChunkWriteFormatFromString(const std::string &value);
std::string ChunkWriteFormatToString(ChunkWriteFormat format);

} // namespace cutum
