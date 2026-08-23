#ifndef CHUNK_H
#define CHUNK_H

#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum
{

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

class UChunk
{
public:
  explicit UChunk(glm::ivec3 chunkCoord);

  glm::ivec3 GetCoord() const { return Coord; }
  BlockId GetBlockLocal(glm::ivec3 local) const;
  void SetBlockLocal(glm::ivec3 local, BlockId Id);
  FluidCellState GetFluidLocal(glm::ivec3 local) const;
  void SetFluidLocal(glm::ivec3 local, FluidCellState state);
  void ClearFluidLocal(glm::ivec3 local);
  bool IsDirty() const { return Dirty; }
  void ClearDirty() { Dirty = false; }
  void MarkDirty() { Dirty = true; }
  /// Recycle for free-list: clear voxels/light/fluid and rebind coord.
  void ResetForReuse(glm::ivec3 chunkCoord);

  const std::array<BlockId, CHUNK_VOLUME> &GetData() const { return Data; }
  const std::array<uint8_t, CHUNK_VOLUME> &GetFluidData() const
  {
    return FluidData;
  }

  uint8_t GetLightPackedLocal(glm::ivec3 local) const;
  int GetSkyLightLocal(glm::ivec3 local) const;
  int GetBlockLightLocal(glm::ivec3 local) const;
  void SetLightLocal(glm::ivec3 local, int sky_level, int block_level);
  void ClearLightLocal(glm::ivec3 local);

  const std::array<uint8_t, CHUNK_VOLUME> &GetLightData() const
  {
    return LightData;
  }
  std::array<uint8_t, CHUNK_VOLUME> &GetLightDataMutable() { return LightData; }

  /// FZ2.7-B1: bumped on any light-field mutation (Apply merge, relight seed).
  uint64_t GetLightFieldRevision() const { return LightFieldRevision; }
  void BumpLightFieldRevision() { ++LightFieldRevision; }

  static int LocalIndex(glm::ivec3 local);

private:
  glm::ivec3 Coord;
  std::array<BlockId, CHUNK_VOLUME> Data{};
  std::array<uint8_t, CHUNK_VOLUME> FluidData{};
  std::array<uint8_t, CHUNK_VOLUME> LightData{};
  uint64_t LightFieldRevision{0};
  bool Dirty{true};
};

} // namespace cutum

#endif
