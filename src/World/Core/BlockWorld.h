#ifndef BLOCKWORLD_H
#define BLOCKWORLD_H

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/MeshCaptureToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Lighting/SceneLightSample.h"
#include "World/Lighting/SectionRenderLightSnapshot.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/WorldVerticalProfile.h"
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace cutum
{

class UBlockDefinitionStorage;
class UChunkBuffer;
struct RelightComputeResult;
struct BlockDefinitionCatalog;

class UBlockWorld
{
public:
  /// Runtime snapshot of the current world's vertical coordinate profile.
  void SetVerticalProfile(WorldVerticalProfile profile)
  {
    VerticalProfile = profile;
  }
  const WorldVerticalProfile &GetVerticalProfile() const
  {
    return VerticalProfile;
  }

  /// Main-thread capture query supplied by the owning world's terrain/IO
  /// completion evidence. A lower resident chunk alone is not proof of sky.
  void SetKnownOpenSkyAbsentQuery(std::function<bool(glm::ivec3)> query)
  {
    KnownAirEvidenceVersionQuery = {};
    LightInputMemo = {};
    KnownAirColumnFloorQuery = {};
    KnownAirInternalSliceQuery = {};
    KnownOpenSkyAbsentQuery = std::move(query);
  }
  struct KnownAirEvidenceVersion
  {
    // Producer identity and revision for terrain and disk evidence.
    std::array<uint64_t, 4> producers{};
    int min_y{0}, max_y{0};
    std::string_view folder;
  };
  /// Production terrain evidence is a column floor: every absent slice above
  /// that floor is proven air. An optional exact internal-section query must
  /// use the same producer/version contract and reject incomplete/pending data.
  /// No evidence is inferred for arbitrary callbacks.
  void SetKnownAirColumnFloorQuery(
      std::function<std::optional<int>(glm::ivec3)> query,
      std::function<KnownAirEvidenceVersion()> version = {},
      std::function<bool(glm::ivec3)> internal_slice_query = {})
  {
    KnownOpenSkyAbsentQuery = {};
    KnownAirColumnFloorQuery = std::move(query);
    KnownAirEvidenceVersionQuery = std::move(version);
    KnownAirInternalSliceQuery = std::move(internal_slice_query);
    LightInputMemo = {};
  }
  bool HasKnownOpenSkyAbsentProof(glm::ivec3 coord) const;

  /// One read-only validation call, never a cache across updates or frames.
  struct InputStampValidationBatch
  {
    const UBlockWorld *owner{nullptr};
    std::unordered_map<glm::ivec3, std::optional<int>, IVec3Hash> air_floors;
  };

  /// Main-thread dependency identity includes the evidence for absent data.
  bool IsKnownAbsentAirChunk(glm::ivec3 coord,
                             InputStampValidationBatch *batch = nullptr) const;
  ChunkInputStamp CaptureInputStamp(glm::ivec3 coord, bool reads_light = true,
                                    bool reads_content = true) const;
  bool InputStampStillMatches(const ChunkInputStamp &stamp,
                              InputStampValidationBatch *batch = nullptr) const;
  bool InputStampLightDomainsStillMatch(
      const ChunkInputStamp &stamp, bool reads_sky, bool reads_block,
      InputStampValidationBatch *batch = nullptr) const;
  void SetLightingConfigurationIdentity(uint64_t identity)
  {
    LightingConfigurationIdentity = identity;
  }
  uint64_t GetLightingConfigurationIdentity() const
  {
    return LightingConfigurationIdentity;
  }

  struct LightInputValidationStats
  {
    uint64_t requests{0}, memo_hits{0}, dependency_validations{0},
        epoch_resets{0};
  };
  const LightInputValidationStats &GetLightInputValidationStats() const
  {
    return LightInputStats;
  }

  /// Record only installed channels, after every write in a validated batch.
  /// Unknown input can support a reusable transient field, never completion.
  int RecordLightCalculation(
      const RelightComputeResult &result,
      const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog);
  /// Diagnostic receipt only: retained inputs may already be stale or unknown.
  /// Never use this context as a readiness certificate.
  std::shared_ptr<const RelightComputeResult>
  GetRecordedLightInputContext(glm::ivec3 coord, LightDomain domain) const;
  LightCalculationState
  GetLightCalculationState(glm::ivec3 coord, LightDomain domain,
                           const std::shared_ptr<const BlockDefinitionCatalog>
                               &current_catalog) const;
  /// Reuse is permitted for an unchanged transient field too. This is not a
  /// completion/readiness query. Each shared dependency context is checked
  /// once.
  bool LightCalculationsStillCurrent(
      const std::vector<glm::ivec3> &sections, bool sky, bool block,
      const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog,
      bool require_padded = false) const;
  bool HasCompleteLightCalculation(
      glm::ivec3 coord,
      const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog)
      const;

  /// Both channels, full padded bytes and complete/current physical inputs.
  bool HasCompleteLightSamplingCalculation(
      glm::ivec3 coord,
      const std::shared_ptr<const BlockDefinitionCatalog> &catalog) const;

  /// Capture only against this world's currently installed material catalog.
  std::shared_ptr<const SectionLightVolume>
  GetCurrentLightSamplingVolume(glm::ivec3 coord) const;

  /// Geometry provenance only; never reads or certifies a light field.
  SectionGeometryRenderInputs CaptureGeometryRenderInputs(
      glm::ivec3 coord,
      std::shared_ptr<const BlockDefinitionCatalog> catalog) const;
  /// Geometry-only inputs against the currently installed material catalog.
  SectionGeometryRenderInputs
  CaptureCurrentGeometryRenderInputs(glm::ivec3 coord) const;

  /// Full current physical proof and immutable GPU-upload bytes, paired with
  /// geometry-only provenance from the same main-thread observation.
  std::optional<SectionRenderLightSnapshot>
  CaptureRenderLightSnapshot(glm::ivec3 coord) const;

  /// Read the canonical field independently of geometry residency. The caller
  /// must select Flat explicitly; runtime configuration identities are opaque.
  SceneLightSample
  SampleSceneLight(glm::ivec3 cell,
                   SceneLightPolicy policy = SceneLightPolicy::Physical) const;

  void SetFluidDefinitions(const UBlockDefinitionStorage *definitions)
  {
    FluidDefinitions = definitions;
  }

  /// Optional gen-scratch mirror: every SetBlock/SetFluid also updates this
  /// buffer (worker populate → ApplyTo live world without a final world scan).
  void SetCaptureBuffer(UChunkBuffer *buffer) { CaptureBuffer = buffer; }
  UChunkBuffer *GetCaptureBuffer() const { return CaptureBuffer; }

  BlockId GetBlock(glm::ivec3 pos) const;
  BlockQueryResult QueryBlock(glm::ivec3 pos) const;
  FluidCellState GetFluidState(glm::ivec3 pos) const;
  void SetBlock(glm::ivec3 pos, BlockId Id);
  /// Apply a block when the owning chunk is already known by the caller.
  /// This avoids repeating the chunk-coordinate hash lookup for bulk chunk IO.
  void SetBlockInChunk(UChunk &chunk, glm::ivec3 pos, BlockId Id);
  void SetFluidState(glm::ivec3 pos, FluidCellState state);
  void SetFluidStateInChunk(UChunk &chunk, glm::ivec3 pos,
                            FluidCellState state);
  void ClearFluidState(glm::ivec3 pos);
  bool IsAir(glm::ivec3 pos) const;
  void Clear();
  size_t CountNonAir() const;
  /// Early-out existence check (UX U2/S6): the coop load path only needs
  /// "is the world empty" - a full scan used to walk every block.
  bool HasAnyNonAir() const;
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunkManager &GetChunkManager() { return Chunks; }
  const UChunkManager &GetChunkManager() const { return Chunks; }

  /// Main-thread immutable band for worker / store (§A.4).
  std::optional<ChunkMeshSnapshot> ReadChunkBandForCapture(
      glm::ivec3 coord, const MeshCaptureToken &token,
      ChunkMeshSnapshot::NeighborVisualDrawableFn neighbor_drawable = nullptr,
      void *neighbor_drawable_ctx = nullptr) const;

private:
  bool StoredLightInputsStillMatch(
      const std::shared_ptr<const RelightComputeResult> &context,
      const std::shared_ptr<const BlockDefinitionCatalog> &catalog) const;
  struct LightInputValidationMemo
  {
    bool initialized{false};
    uint64_t input_revision{0};
    std::array<uint64_t, 4> evidence_producers{};
    int min_y{0}, max_y{0};
    std::string folder;
    WorldVerticalProfile profile;
    uint64_t lighting_configuration{0};
    std::shared_ptr<const BlockDefinitionCatalog> catalog;
    struct Entry
    {
      std::weak_ptr<const RelightComputeResult> input;
      std::shared_ptr<const uint64_t> input_identity;
      bool current{false};
    };
    std::unordered_map<const RelightComputeResult *, Entry> entries;
  };
  mutable LightInputValidationMemo LightInputMemo;
  mutable LightInputValidationStats LightInputStats;
  UChunkManager Chunks;
  std::shared_ptr<const void> LightCalculationOwner{
      std::make_shared<const int>(0)};
  WorldVerticalProfile VerticalProfile{};
  uint64_t LightingConfigurationIdentity{0};
  const UBlockDefinitionStorage *FluidDefinitions{nullptr};
  UChunkBuffer *CaptureBuffer{nullptr};
  std::function<bool(glm::ivec3)> KnownOpenSkyAbsentQuery;
  std::function<std::optional<int>(glm::ivec3)> KnownAirColumnFloorQuery;
  std::function<KnownAirEvidenceVersion()> KnownAirEvidenceVersionQuery;
  std::function<bool(glm::ivec3)> KnownAirInternalSliceQuery;
};

} // namespace cutum

#endif
