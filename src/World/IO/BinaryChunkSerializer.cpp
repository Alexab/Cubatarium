#include "World/IO/BinaryChunkSerializer.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace cutum
{

namespace
{

struct ChunkWritePalette
{
  // Palette index zero is reserved for AIR and marks an unoccupied slot.
  // At most CHUNK_VOLUME non-air values keep the table at most half full.
  std::array<uint16_t, 2 * CHUNK_VOLUME> Slots{};
  std::array<BlockId, CHUNK_VOLUME + 1> Values;
  uint16_t Count{1};

  ChunkWritePalette() { Values[0] = BLOCK_AIR; }

  void Include(BlockId id)
  {
    if (id == BLOCK_AIR)
      return;
    auto &entry = Slots[FindSlot(id)];
    if (!entry)
    {
      Values[Count] = id;
      entry = Count++;
    }
  }

  uint16_t Index(BlockId id) const
  {
    return id == BLOCK_AIR ? 0 : Slots[FindSlot(id)];
  }

private:
  size_t FindSlot(BlockId id) const
  {
    // The high bits spread the 16-bit IDs across the fixed table.
    const uint32_t hash = uint32_t{id} * 2654435761u;
    size_t slot = (hash >> 16) % Slots.size();
    while (Slots[slot] && Values[Slots[slot]] != id)
      slot = (slot + 1) % Slots.size();
    return slot;
  }
};

template<class Visitor>
size_t VisitChunkRuns(const std::array<BlockId, CHUNK_VOLUME> &data, Visitor visit)
{
  size_t count = 0;
  for (auto cell = data.begin(); cell != data.end(); ++count)
  {
    const BlockId id = *cell;
    const auto next = std::find_if(cell + 1, data.end(), [id](BlockId other) { return other != id; });
    visit(id, static_cast<uint16_t>(next - cell));
    cell = next;
  }
  return count;
}

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

void AppendU64(std::vector<uint8_t> &out, uint64_t value)
{
  for (int shift = 0; shift < 64; shift += 8)
  {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
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

bool ReadU64(const std::vector<uint8_t> &bytes, size_t &offset, uint64_t &value)
{
  if (offset + 8 > bytes.size())
  {
    return false;
  }
  value = 0;
  for (int shift = 0; shift < 64; shift += 8)
  {
    value |= static_cast<uint64_t>(bytes[offset++]) << shift;
  }
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

SerializedChunk
UBinaryChunkSerializer::Serialize(glm::ivec3 chunkCoord, const UChunk &chunk,
                                  UBlockRegistry & /*registry*/,
                                  const IoCodecProbe *probe) const
{
  SerializedChunk out;
  out.format = ChunkDiskFormat::Binary;

  ChunkWritePalette palette;
  const auto &data = chunk.GetData();
  const auto runs = VisitChunkRuns(data, [&](BlockId id, uint16_t) { palette.Include(id); });
  constexpr size_t fixed_bytes = kMaxWrittenFrameBytes - 2 * (CHUNK_VOLUME + 1) - 4 * CHUNK_VOLUME;
  std::vector<uint8_t> &bytes = out.bytes;
  bytes.reserve(fixed_bytes + 2 * palette.Count + 4 * runs);
  bytes.insert(bytes.end(), std::begin(kMagic), std::end(kMagic));
  bytes.push_back(kVersion);
  AppendI32(bytes, chunkCoord.x);
  AppendI32(bytes, chunkCoord.y);
  AppendI32(bytes, chunkCoord.z);
  AppendU16(bytes, palette.Count);
  for (uint16_t index = 0; index < palette.Count; ++index)
    AppendU16(bytes, palette.Values[index]);
  AppendU32(bytes, static_cast<uint32_t>(runs));
  VisitChunkRuns(data, [&](BlockId id, uint16_t length) {
    AppendU16(bytes, length);
    AppendU16(bytes, palette.Index(id));
  });

  AppendU32(bytes, static_cast<uint32_t>(CHUNK_VOLUME));
  bytes.insert(bytes.end(), chunk.GetFluidData().begin(),
               chunk.GetFluidData().end());
  AppendU32(bytes, static_cast<uint32_t>(CHUNK_VOLUME));
  bytes.insert(bytes.end(), chunk.GetLightData().begin(),
               chunk.GetLightData().end());
  const float wetness = std::clamp(chunk.GetSurfaceWetness(), 0.0f, 1.0f);
  AppendU16(bytes, static_cast<uint16_t>(std::lround(wetness * 65535.0f)));
  const double last_update_sec = std::max(0.0, chunk.GetWeatherLastUpdateSec());
  const double milliseconds = last_update_sec * 1000.0;
  const uint64_t last_update_ms =
      !std::isfinite(milliseconds) ||
              milliseconds >=
                  static_cast<double>(std::numeric_limits<uint64_t>::max())
          ? std::numeric_limits<uint64_t>::max()
          : static_cast<uint64_t>(milliseconds + 0.5);
  AppendU64(bytes, last_update_ms);
  if (probe)
  {
    StlSourceStorage output;
    output.Buffer(bytes.capacity(), sizeof(uint8_t));
    probe->sample(probe->context, {IoCodecKind::BinaryWrite, 0, 0, output.bytes.requested,
                                  sizeof(palette), output.bytes.supported});
  }
  return out;
}

UChunkBuffer
UBinaryChunkSerializer::Deserialize(const std::vector<uint8_t> &bytes,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry,
                                    const IoCodecProbe *probe) const
{
  UChunkBuffer buffer;
  if (bytes.size() < 4 + 1 + 12 + 2 || bytes.size() > kMaxFrameBytes)
  {
    return buffer;
  }

  if (std::memcmp(bytes.data(), kMagic, 4) != 0)
  {
    return buffer;
  }

  size_t offset = 4;
  const uint8_t version = bytes[offset++];
  if (version != kVersion && version != kVersionLight &&
      version != kVersionFluid && version != kVersionLegacy)
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
  const bool coordinate_matches = glm::ivec3(cx, cy, cz) == chunkCoord;

  uint16_t paletteCount = 0;
  if (!ReadU16(bytes, offset, paletteCount) || !paletteCount ||
      paletteCount > (bytes.size() - offset) / 2)
  {
    return buffer;
  }

  const size_t palette_start = offset;
  offset += size_t(paletteCount) * 2;

  uint32_t runCount = 0;
  if (!ReadU32(bytes, offset, runCount) || !runCount || runCount > CHUNK_VOLUME)
  {
    return buffer;
  }
  const size_t footer_bytes =
      (version >= kVersionFluid ? 4 + CHUNK_VOLUME : 0) +
      (version >= kVersionLight ? 4 + CHUNK_VOLUME : 0) +
      (version >= kVersion ? 2 + 8 : 0);
  if (bytes.size() - offset < size_t(runCount) * 4 + footer_bytes)
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
    if (!length || paletteIdx >= paletteCount || length > CHUNK_VOLUME - filled)
    {
      buffer.Clear();
      return buffer;
    }
    size_t palette_entry = palette_start + size_t(paletteIdx) * 2;
    BlockId id = BLOCK_AIR;
    ReadU16(bytes, palette_entry, id);
    for (uint16_t i = 0; i < length; ++i)
    {
      if (id != BLOCK_AIR)
      {
        const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + lx,
                                  chunkCoord.y * CHUNK_SIZE + ly,
                                  chunkCoord.z * CHUNK_SIZE + lz);
        buffer.SetBlock(worldPos, id);
        if (version == kVersionLegacy && registry.IsLiquid(id))
        {
          buffer.SetFluidPacked(worldPos,
                                PackFluidCellState(FluidCellState::Source()));
        }
      }
      advanceLocal();
      ++filled;
    }
  }

  if (filled != CHUNK_VOLUME)
  {
    buffer.Clear();
    return buffer;
  }

  if (version >= kVersionFluid)
  {
    uint32_t fluid_byte_count = 0;
    if (!ReadU32(bytes, offset, fluid_byte_count) || fluid_byte_count != CHUNK_VOLUME)
    {
      buffer.Clear();
      return buffer;
    }
    int fx = 0;
    int fy = 0;
    int fz = 0;
    for (uint32_t fi = 0; fi < fluid_byte_count; ++fi)
    {
      const uint8_t packed = bytes[offset++];
      if (packed != 0)
      {
        const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + fx,
                                  chunkCoord.y * CHUNK_SIZE + fy,
                                  chunkCoord.z * CHUNK_SIZE + fz);
        buffer.SetFluidPacked(worldPos, packed);
      }
      ++fx;
      if (fx >= CHUNK_SIZE)
      {
        fx = 0;
        ++fy;
        if (fy >= CHUNK_SIZE)
        {
          fy = 0;
          ++fz;
        }
      }
    }
  }

  if (version >= kVersionLight)
  {
    uint32_t light_byte_count = 0;
    if (!ReadU32(bytes, offset, light_byte_count) || light_byte_count != CHUNK_VOLUME)
    {
      buffer.Clear();
      return buffer;
    }
    std::array<uint8_t, CHUNK_VOLUME> light{};
    std::memcpy(light.data(), bytes.data() + offset, CHUNK_VOLUME);
    offset += CHUNK_VOLUME;
    buffer.SetChunkLightData(chunkCoord, light);
  }

  if (version >= kVersion)
  {
    uint16_t wetness_packed = 0;
    uint64_t last_update_ms = 0;
    if (!ReadU16(bytes, offset, wetness_packed) ||
        !ReadU64(bytes, offset, last_update_ms))
    {
      buffer.Clear();
      return buffer;
    }
    buffer.SetChunkWeatherState(chunkCoord,
                                static_cast<float>(wetness_packed) / 65535.0f,
                                static_cast<double>(last_update_ms) / 1000.0);
  }

  // A bounded decoded prefix can still carry geometry for diagnostics.
  // Only the exact frame may be applied as terrain.
  if (coordinate_matches && offset == bytes.size())
    buffer.MarkDecodedFrame(chunkCoord);
  if (probe)
  {
    StlSourceStorage input;
    input.Buffer(bytes.capacity(), sizeof(uint8_t));
    probe->sample(probe->context, {IoCodecKind::BinaryRead, 0, input.bytes.requested,
                                  buffer.GetOwnedAllocationBytes(),
                                  version >= kVersionLight ? size_t(CHUNK_VOLUME) : 0,
                                  input.bytes.supported});
  }
  return buffer;
}

} // namespace cutum
