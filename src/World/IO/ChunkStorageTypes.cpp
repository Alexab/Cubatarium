#include "World/IO/ChunkStorageTypes.h"

namespace cutum
{

ChunkWriteFormat ChunkWriteFormatFromString(const std::string &value)
{
  if (value == "json")
  {
    return ChunkWriteFormat::Json;
  }
  return ChunkWriteFormat::Binary;
}

std::string ChunkWriteFormatToString(ChunkWriteFormat format)
{
  switch (format)
  {
  case ChunkWriteFormat::Json:
    return "json";
  case ChunkWriteFormat::Binary:
  default:
    return "binary";
  }
}

} // namespace cutum
