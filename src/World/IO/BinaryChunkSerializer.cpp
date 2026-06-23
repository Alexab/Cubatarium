#include "World/IO/BinaryChunkSerializer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace cutum
{

namespace
{

void AppendU16(std::vector<uint8_t> &out, uint16_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<uint8_t> &out, uint32_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void AppendI32(std::vector<uint8_t> &out, int32_t value)
{
  AppendU32(out, static_cast<uint32_t>(value));
}

bool ReadU16(const std::vector<uint8_t> &bytes, size_t &offset, uint16_t &value)
{
  if (offset + 2 > bytes.size())
  {
    return false;
  }
  value = static_cast<uint16_t>(bytes[offset]) |
          (static_cast<uint16_t>(bytes[offset + 1]) << 8);
  offset += 2;
  return true;
}

bool ReadU32(const std::vector<uint8_t> &bytes, size_t &offset, uint32_t &value)
{
  if (offset + 4 > bytes.size())
  {
    return false;
  }
  value = static_cast<uint32_t>(bytes[offset]) |
          (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
          (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
          (static_cast<uint32_t>(bytes[offset + 3]) << 24);
  offset += 4;
  return true;
}

bool ReadI32(const std::vector<uint8_t> &bytes, size_t &offset, int32_t &value)
{
  uint32_t raw = 0;
  if (!ReadU32(bytes, offset, raw))
  {
    return false;
  }
  value = static_cast<int32_t>(raw);
  return true;
}

} // namespace

SerializedChunk BinaryChunkSerializer::Serialize(glm::ivec3 chunkCoord,
                                                 const UChunk &chunk,
                                                 UBlockRegistry & /*registry*/) const
{
  SerializedChunk out;
  out.format = ChunkDiskFormat::Binary;

  std::unordered_map<BlockId, uint16_t> paletteIndex;
  std::vector<BlockId> palette;
  palette.push_back(BLOCK_AIR);
  paletteIndex[BLOCK_AIR] = 0;

  auto paletteFor = [&](BlockId id) -> uint16_t
  {
    const auto it = paletteIndex.find(id);
    if (it != paletteIndex.end())
    {
      return it->second;
    }
    const uint16_t index = static_cast<uint16_t>(palette.size());
    palette.push_back(id);
    paletteIndex[id] = index;
    return index;
  };

  struct Run
  {
    uint16_t length{0};
    uint16_t paletteIdx{0};
  };
  std::vector<Run> runs;
  Run currentRun{0, 0};
  bool hasRun = false;

  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const BlockId id = chunk.GetBlockLocal(glm::ivec3(x, y, z));
        const uint16_t paletteIdx = paletteFor(id);
        if (!hasRun)
        {
          currentRun = Run{1, paletteIdx};
          hasRun = true;
          continue;
        }
        if (currentRun.paletteIdx == paletteIdx &&
            currentRun.length < UINT16_MAX)
        {
          ++currentRun.length;
          continue;
        }
        runs.push_back(currentRun);
        currentRun = Run{1, paletteIdx};
      }
    }
  }
  if (hasRun)
  {
    runs.push_back(currentRun);
  }

  std::vector<uint8_t> &bytes = out.bytes;
  bytes.insert(bytes.end(), std::begin(kMagic), std::end(kMagic));
  bytes.push_back(kVersion);
  AppendI32(bytes, chunkCoord.x);
  AppendI32(bytes, chunkCoord.y);
  AppendI32(bytes, chunkCoord.z);
  AppendU16(bytes, static_cast<uint16_t>(palette.size()));
  for (const BlockId id : palette)
  {
    AppendU16(bytes, static_cast<uint16_t>(id));
  }
  AppendU32(bytes, static_cast<uint32_t>(runs.size()));
  for (const Run &run : runs)
  {
    AppendU16(bytes, run.length);
    AppendU16(bytes, run.paletteIdx);
  }
  return out;
}

ChunkBuffer BinaryChunkSerializer::Deserialize(const std::vector<uint8_t> &bytes,
                                               glm::ivec3 chunkCoord,
                                               UBlockRegistry & /*registry*/) const
{
  ChunkBuffer buffer;
  if (bytes.size() < 4 + 1 + 12 + 2)
  {
    return buffer;
  }

  if (std::memcmp(bytes.data(), kMagic, 4) != 0)
  {
    return buffer;
  }

  size_t offset = 4;
  const uint8_t version = bytes[offset++];
  if (version != kVersion)
  {
    return buffer;
  }

  int32_t cx = 0;
  int32_t cy = 0;
  int32_t cz = 0;
  if (!ReadI32(bytes, offset, cx) || !ReadI32(bytes, offset, cy) ||
      !ReadI32(bytes, offset, cz))
  {
    return buffer;
  }
  (void)cx;
  (void)cy;
  (void)cz;

  uint16_t paletteCount = 0;
  if (!ReadU16(bytes, offset, paletteCount))
  {
    return buffer;
  }

  std::vector<BlockId> palette;
  palette.reserve(paletteCount);
  for (uint16_t i = 0; i < paletteCount; ++i)
  {
    uint16_t rawId = 0;
    if (!ReadU16(bytes, offset, rawId))
    {
      return buffer;
    }
    palette.push_back(static_cast<BlockId>(rawId));
  }

  uint32_t runCount = 0;
  if (!ReadU32(bytes, offset, runCount))
  {
    return buffer;
  }

  int lx = 0;
  int ly = 0;
  int lz = 0;
  auto advanceLocal = [&]()
  {
    ++lx;
    if (lx >= CHUNK_SIZE)
    {
      lx = 0;
      ++ly;
      if (ly >= CHUNK_SIZE)
      {
        ly = 0;
        ++lz;
      }
    }
  };

  size_t filled = 0;
  for (uint32_t r = 0; r < runCount; ++r)
  {
    uint16_t length = 0;
    uint16_t paletteIdx = 0;
    if (!ReadU16(bytes, offset, length) || !ReadU16(bytes, offset, paletteIdx))
    {
      buffer.Clear();
      return buffer;
    }
    if (paletteIdx >= palette.size())
    {
      buffer.Clear();
      return buffer;
    }
    const BlockId id = palette[paletteIdx];
    for (uint16_t i = 0; i < length; ++i)
    {
      if (filled >= CHUNK_VOLUME || lz >= CHUNK_SIZE)
      {
        buffer.Clear();
        return buffer;
      }
      if (id != BLOCK_AIR)
      {
        const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + lx,
                                chunkCoord.y * CHUNK_SIZE + ly,
                                chunkCoord.z * CHUNK_SIZE + lz);
        buffer.SetBlock(worldPos, id);
      }
      advanceLocal();
      ++filled;
    }
  }

  if (filled != CHUNK_VOLUME)
  {
    buffer.Clear();
  }
  return buffer;
}

} // namespace cutum
