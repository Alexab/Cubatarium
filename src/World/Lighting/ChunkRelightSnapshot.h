#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Lighting/LightUtil.h"
#include "World/Lighting/RelightContentCertificate.h"
#include "World/Lighting/SectionLightVolume.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/WorldVerticalProfile.h"
#include "World/Streaming/WorkToken.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct RelightJobSpec
{
  std::vector<glm::ivec3> block_positions;
  int min_world_y{0};
  int max_world_y{319};
  bool include_block_light{true};
  bool include_skylight{true};
  int frontier_iterations{2};
  uint64_t job_id{0};
  /// False while more Y-bands remain for this column (SoftDefer keeps Pending).
  bool finalize_pending_gate{true};
  /// Renderer-rejected exact slice may remesh before the column gate finalizes.
  bool visible_draw_gate_repair{false};
  /// Write only the center column; neighboring geometry remains read-only.
  bool column_center_only{false};
  /// Explicit demand for AIR fields; does not allocate geometry.
  bool include_known_air_outputs{false};
  /// Exact additional AIR consumers for column jobs, including outside the
  /// terrain Y-band. Other empty sections remain read-only; no intermediate
  /// resident section outside that band becomes an output.
  std::vector<glm::ivec3> known_air_output_sections;
  /// Geometry-independent sample targets, resident or proven AIR. Explicit
  /// targets can lie outside the terrain band; intermediate slices stay
  /// read-only.
  std::vector<glm::ivec3> light_sample_output_sections;
};

struct RelightChunkLightData
{
  glm::ivec3 coord{0};
  std::array<uint8_t, CHUNK_VOLUME> light_packed{};
  /// Same calculation/read-set as the interior; null for legacy raw results.
  std::shared_ptr<const SectionLightVolume> padded_light;
};

/// CheapRemesh C3: skip MarkRelit Dirty when Apply light equals current chunk.
inline bool
PrimaryLightUnchanged(const std::array<uint8_t, CHUNK_VOLUME> &before,
                      const std::array<uint8_t, CHUNK_VOLUME> &after)
{
  return std::memcmp(before.data(), after.data(), CHUNK_VOLUME) == 0;
}

/// FZ2.7-B: after GPU sky seed, skip packed merge when block nibbles match.
inline bool BlockLightUnchanged(const std::array<uint8_t, CHUNK_VOLUME> &before,
                                const std::array<uint8_t, CHUNK_VOLUME> &after)
{
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    if (UnpackBlock(before[static_cast<size_t>(i)]) !=
        UnpackBlock(after[static_cast<size_t>(i)]))
    {
      return false;
    }
  }
  return true;
}

/// Observed solver work; never used as proof of light/input readiness.
struct RelightFloodWork
{
  uint64_t candidate_seeds{0};
  uint64_t queued_seeds{0};
  uint64_t popped_nodes{0};
  uint64_t neighbor_visits{0};
  uint64_t propagated_cells{0};
  uint64_t saturated_tiles{0};
};

struct RelightComputeResult
{
  uint64_t job_id{0};
  uint64_t submitEpoch{0};
  WorkToken work_token{};
  DependencyStamp dependency_stamp{};
  std::vector<ChunkInputStamp> read_set;
  RelightJobSpec retry_spec;
  std::shared_ptr<const BlockDefinitionCatalog> input_catalog;
  WorldVerticalProfile input_profile;
  uint64_t input_lighting_configuration{0};
  bool input_context_captured{false};
  bool inputs_complete{false};
  /// Set from the immutable read set when retaining a calculation context.
  /// Raw result flags never replace validation of the actual input stamps.
  bool retained_inputs_geometry_only{false};
  std::shared_ptr<const RelightContentCertificate> content_certificate;
  int unknown_input_chunks{0};
  std::vector<RelightChunkLightData> chunks;
  bool frontier_unfinished{false};
  bool finalize_pending_gate{true};
  bool visible_draw_gate_repair{false};
  bool include_skylight{true};
  bool include_block_light{true};
  std::vector<glm::ivec3> source_block_positions;
  RelightFloodWork sky_flood_work;
  RelightFloodWork block_flood_work;
};

/// Immutable geometry input for relight with a full-section influence halo.
class UChunkRelightSnapshot
{
public:
  static UChunkRelightSnapshot Capture(const UBlockWorld &world,
                                       const RelightJobSpec &spec);

  /// Explicit write set for synchronous batches; geometry halo remains
  /// read-only.
  static UChunkRelightSnapshot
  CaptureSections(const UBlockWorld &world, const RelightJobSpec &spec,
                  const std::vector<glm::ivec3> &sections);

  RelightComputeResult Compute(const UBlockRegistry &registry);
  void SetInputCatalog(std::shared_ptr<const BlockDefinitionCatalog> catalog)
  {
    InputCatalog = std::move(catalog);
    InputCatalogPinned = true;
  }
  uint64_t GetJobId() const { return Spec.job_id; }
  const WorkToken &GetWorkToken() const { return CapturedWorkToken; }
  const DependencyStamp &GetDependencyStamp() const { return CapturedDepStamp; }
  void SetSubmitContext(WorkToken token, DependencyStamp deps)
  {
    CapturedWorkToken = token;
    CapturedDepStamp = deps;
  }
  int GetCapturedFullChunks() const { return CapturedFullChunks; }
  int GetCapturedNeighborLightChunks() const
  {
    return CapturedNeighborLightChunks;
  }
  int GetCapturedGeometryHaloChunks() const
  {
    return CapturedGeometryHaloChunks;
  }
  int GetCapturedSkyContextChunks() const { return CapturedSkyContextChunks; }
  double GetSkyContextCaptureMs() const { return SkyContextCaptureMs; }
  int GetWorldMaxY() const { return WorldMaxY; }
  int GetWorldMinY() const { return InputProfile.MinY; }

  BlockId GetBlock(glm::ivec3 world_pos) const;
  bool HasChunk(glm::ivec3 chunk_coord) const;
  bool HasComputeChunk(glm::ivec3 chunk_coord) const;
  bool IsKnownGeometry(glm::ivec3 chunk_coord) const;
  const std::array<BlockId, CHUNK_VOLUME> *GeometryData(glm::ivec3 coord) const;
  bool HasLight(glm::ivec3 chunk_coord) const;
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
  static UChunkRelightSnapshot
  CaptureImpl(const UBlockWorld &world, const RelightJobSpec &spec,
              const std::vector<glm::ivec3> *sections);
  std::unordered_map<glm::ivec3, std::array<BlockId, CHUNK_VOLUME>, IVec3Hash>
      Blocks;
  /// Geometry read-only context above the relight target band. It participates
  /// in skylight occlusion and input validation, but is never relit or applied.
  std::unordered_map<glm::ivec3, std::array<BlockId, CHUNK_VOLUME>, IVec3Hash>
      SkyContextBlocks;
  std::unordered_map<glm::ivec3, std::array<uint8_t, CHUNK_VOLUME>, IVec3Hash>
      Light;
  std::unordered_set<glm::ivec3, IVec3Hash> WriteChunks;
  std::unordered_set<glm::ivec3, IVec3Hash> ComputeChunks;
  std::unordered_set<glm::ivec3, IVec3Hash> KnownGeometry;
  RelightJobSpec Spec;
  WorkToken CapturedWorkToken{};
  DependencyStamp CapturedDepStamp{};
  std::vector<ChunkInputStamp> ReadSet;
  int CapturedFullChunks{0};
  int CapturedNeighborLightChunks{0};
  int CapturedGeometryHaloChunks{0};
  int CapturedSkyContextChunks{0};
  double SkyContextCaptureMs{0.0};
  int WorldMaxY{0};
  WorldVerticalProfile InputProfile;
  uint64_t InputLightingConfiguration{0};
  bool InputContextCaptured{false};
  std::shared_ptr<const BlockDefinitionCatalog> InputCatalog;
  bool InputCatalogPinned{false};
};

} // namespace cutum
