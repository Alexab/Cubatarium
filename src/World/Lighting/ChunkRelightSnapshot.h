#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct RelightJobSpec
{
  std::vector<glm::ivec3> block_positions;
  int min_world_y{0};
  int max_world_y{128};
  bool include_block_light{true};
  int frontier_iterations{2};
  uint64_t job_id{0};
};

struct RelightChunkLightData
{
  glm::ivec3 coord{0};
  std::array<uint8_t, CHUNK_VOLUME> light_packed{};
};

struct RelightComputeResult
{
  uint64_t job_id{0};
  std::vector<RelightChunkLightData> chunks;
  bool frontier_unfinished{false};
  std::vector<glm::ivec3> source_block_positions;
};

/// Read-only voxel grid for background relight (center chunks + one-block shell).
class UChunkRelightSnapshot
{
public:
  static UChunkRelightSnapshot Capture(const UBlockWorld &world,
                                       const RelightJobSpec &spec);

  RelightComputeResult Compute(const UBlockRegistry &registry) const;
  uint64_t GetJobId() const { return Spec.job_id; }

  BlockId GetBlock(glm::ivec3 world_pos) const;
  bool HasChunk(glm::ivec3 chunk_coord) const;
  int GetSkyLight(glm::ivec3 world_pos) const;
  int GetBlockLight(glm::ivec3 world_pos) const;
  void WriteSkyLight(glm::ivec3 world_pos, int level);
  void WriteBlockLight(glm::ivec3 world_pos, int level);
  int GetSkyLightLocal(glm::ivec3 chunk_coord, glm::ivec3 local) const;
  int GetBlockLightLocal(glm::ivec3 chunk_coord, glm::ivec3 local) const;
  void ClearChunkLight(glm::ivec3 chunk_coord);
  void SetLightLocal(glm::ivec3 chunk_coord, glm::ivec3 local, int sky,
                     int block_level);

private:
  std::unordered_map<glm::ivec3, std::array<BlockId, CHUNK_VOLUME>, IVec3Hash>
      Blocks;
  std::unordered_map<glm::ivec3, std::array<uint8_t, CHUNK_VOLUME>, IVec3Hash>
      Light;
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> ShellBlocks;
  RelightJobSpec Spec;
};

} // namespace cutum
