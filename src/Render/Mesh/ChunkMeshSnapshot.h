#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include "Render/Mesh/MeshNeighborPolicy.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

/// Read-only voxel view for background meshing (center chunk + one-block
/// shell). Shell faces are dense arrays (6 * CHUNK_SIZE^2), not hash maps.
struct ChunkMeshSnapshot
{
  static constexpr int kShellFaceCount = 6;
  static constexpr int kShellFaceCells = CHUNK_SIZE * CHUNK_SIZE;
  static constexpr int kShellCells = kShellFaceCount * kShellFaceCells;

  glm::ivec3 coord{0};
  std::array<BlockId, CHUNK_VOLUME> blocks{};
  std::array<uint8_t, CHUNK_VOLUME> fluid_packed{};
  std::array<uint8_t, CHUNK_VOLUME> light_packed{};
  std::array<BlockId, kShellCells> shellBlocks{};
  std::array<uint8_t, kShellCells> shellFluid{};
  std::array<uint8_t, kShellCells> shellLight{};
  std::array<uint8_t, kShellCells> shellNeighborState{};
  uint64_t sourceRevision{0};

  static ChunkMeshSnapshot Capture(const UBlockWorld &world,
                                   glm::ivec3 chunkCoord,
                                   uint64_t sourceRevision);

  BlockId GetBlock(glm::ivec3 worldPos) const;
  BlockId GetBlockLocal(glm::ivec3 local) const;
  uint8_t GetLightPackedLocal(glm::ivec3 local) const;
  uint8_t GetLightPacked(glm::ivec3 worldPos) const;
  uint8_t GetFluidPackedLocal(glm::ivec3 local) const;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  FluidCellState GetFluidLocal(glm::ivec3 local) const;
  FluidCellState GetFluid(glm::ivec3 worldPos) const;
  glm::ivec3 ChunkOrigin() const;
  NeighborLoadState GetNeighborLoadState(glm::ivec3 worldPos) const;
};

} // namespace cutum
