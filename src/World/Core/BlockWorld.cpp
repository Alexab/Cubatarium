#include "World/Core/BlockWorld.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidKindPresetUtil.h"
#include "World/Physics/FluidPermeabilityUtil.h"
#include "WorldGen/Core/RuntimeSettingsText.h"
#include <algorithm>
#include <unordered_set>

namespace cutum
{
namespace {
template<class Table> size_t EmptyPipelineTableRequest() noexcept {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  using Node=std::_List_node<typename Table::value_type,void*>;
  return sizeof(Node)+8*2*sizeof(typename Table::iterator);
#else
  return 0;
#endif
}
}

std::array<size_t,4> UBlockWorld::GetPipelineScratchStorageLayout() noexcept
{
  std::array<size_t,4> out{};
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  // Default xhash constructs one sentinel and8 buckets. This STL's deque
  // always derives _Container_base12,so its proxy exists even in Release;
  // empty deque map/blocks and vector backing are otherwise absent.
  out[0]=EmptyPipelineTableRequest<decltype(UChunkManager::Chunks)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::LightFields)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::LoadedAirSlices)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::DecodedTerrainSlices)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::UnresolvedTerrainColumnReads)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::ResidentColumnChunkCounts)>()+
    EmptyPipelineTableRequest<decltype(UChunkManager::ResidentColumnTerrainCounts)>()+sizeof(std::_Container_proxy);
  out[1]=EmptyPipelineTableRequest<decltype(LightInputValidationMemo::entries)>();
  out[2]=2*sizeof(std::_Ref_count_obj2<uint64_t>)+sizeof(std::_Ref_count_obj2<const int>);
  out[3]=sizeof(std::_Ref_count_obj2<UConcurrentAllocationCountingResource>);
#endif
  return out;
}

PipelineScratchStorage UBlockWorld::InspectPipelineScratchStorage(bool default_factory_profile) const noexcept
{
  PipelineScratchStorage out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=default_factory_profile && !CaptureBuffer && !KnownOpenSkyAbsentQuery && !KnownAirColumnFloorQuery &&
    !KnownAirEvidenceVersionQuery && !KnownAirInternalSliceQuery && !LightInputMemo.initialized && !LightInputMemo.catalog &&
    LightInputMemo.entries.empty() && !ExternalStringCapacityBytes(LightInputMemo.folder) &&
    Chunks.Chunks.empty() && Chunks.LightFields.empty() && Chunks.LoadedAirSlices.empty() && Chunks.DecodedTerrainSlices.empty() &&
    Chunks.UnresolvedTerrainColumnReads.empty() && Chunks.AbsentLightFieldScan.empty() &&
    Chunks.ResidentColumnChunkCounts.empty() && Chunks.ResidentColumnTerrainCounts.empty() && Chunks.FreeList.empty() &&
    !Chunks.BeforeChunkAllocation && Chunks.InputRevision && Chunks.GeometryInputRevision && LightCalculationOwner;
  out.metadata=Chunks.GetMetadataAllocationSnapshot().live_bytes;
  out.memo=sizeof(std::_List_node<typename decltype(LightInputMemo.entries)::value_type,void*>)+
    LightInputMemo.entries.bucket_count()*2*sizeof(typename decltype(LightInputMemo.entries)::iterator);
  const std::array<std::weak_ptr<const void>,3> clocks={Chunks.InputRevision,Chunks.GeometryInputRevision,LightCalculationOwner};
  const std::array<size_t,3> widths={sizeof(std::_Ref_count_obj2<uint64_t>),sizeof(std::_Ref_count_obj2<uint64_t>),sizeof(std::_Ref_count_obj2<const int>)};
  for(size_t i=0;i<clocks.size();++i){bool seen=false;
    for(size_t j=0;j<i;++j)if(!clocks[i].owner_before(clocks[j]) && !clocks[j].owner_before(clocks[i]))seen=true;
    if(!seen)out.controls+=widths[i];
  }
  out.allocator=reinterpret_cast<uintptr_t>(Chunks.GetMetadataAllocationResource().get());
#endif
  return out;
}

int UBlockWorld::RecordLightCalculation(
    const RelightComputeResult &result,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog)
{
  if (ValidateRelightInputs(result, *this, current_catalog) !=
      RelightInputValidation::Current)
    return 0;
  if (!LightCalculationOwner)
    LightCalculationOwner = std::make_shared<const int>(0);
  // Retain provenance once per batch, never its output voxel arrays or retry
  // payload. All section/domain records share this immutable input context.
  auto context = std::make_shared<RelightComputeResult>();
  context->read_set = result.read_set;
  context->retained_inputs_geometry_only = std::none_of(
      context->read_set.begin(), context->read_set.end(),
      [](const ChunkInputStamp &stamp) { return stamp.readsLight; });
  context->input_catalog = result.input_catalog;
  context->input_profile = result.input_profile;
  context->input_lighting_configuration = result.input_lighting_configuration;
  context->input_context_captured = result.input_context_captured;
  context->inputs_complete =
      ValidateRelightInputs(result, *this, current_catalog, true) ==
      RelightInputValidation::Current;
  context->unknown_input_chunks = result.unknown_input_chunks;
  context->include_skylight = result.include_skylight;
  context->include_block_light = result.include_block_light;
  if (context->inputs_complete && result.content_certificate &&
      result.content_certificate->MatchesInputs(
          result.read_set, result.input_profile,
          result.input_lighting_configuration, result.input_catalog,
          result.include_skylight, result.include_block_light))
    context->content_certificate = result.content_certificate;
  int recorded = 0;
  for (const auto &data : result.chunks)
  {
    auto *chunk = Chunks.GetChunk(data.coord);
    const auto field_it = Chunks.LightFields.find(data.coord);
    if (field_it == Chunks.LightFields.end())
      continue;
    auto *field = field_it->second.get();
    bool owns_geometry = false;
    for (const auto &stamp : result.read_set)
      if (stamp.coord == data.coord && stamp.readsContent &&
          ((chunk && stamp.incarnation == chunk->GetIncarnation()) ||
           (!chunk && stamp.missingEvidence == MissingChunkEvidence::KnownAir)))
      {
        owns_geometry = true;
        break;
      }
    if (!owns_geometry)
      continue;
    bool any = false;
    for (int domain = 0; domain < 2; ++domain)
    {
      const bool sky = domain == 0;
      if (!(sky ? result.include_skylight : result.include_block_light))
        continue;
      const uint8_t mask = sky ? 0x0f : 0xf0;
      bool installed = true;
      for (int i = 0; i < CHUNK_VOLUME; ++i)
        if ((field->GetData()[i] & mask) != (data.light_packed[i] & mask))
        {
          installed = false;
          break;
        }
      if (!installed)
        continue; // publication was omitted or overwritten
      auto &proof = field->LightProofs[domain];
      proof.padded_installed = false;
      const auto padded = field->GetPaddedData();
      if (data.padded_light && padded &&
          (field->GetPaddedDomains() & (sky ? 1 : 2)))
      {
        proof.padded_installed = true;
        for (size_t i = 0; i < padded->cells.size(); ++i)
          if (((padded->cells[i] ^ data.padded_light->cells[i]) & mask) != 0)
          {
            proof.padded_installed = false;
            break;
          }
      }
      proof.input = context;
      proof.owner = LightCalculationOwner;
      proof.incarnation = field->GetIncarnation();
      proof.field_revision =
          sky ? field->GetSkyRevision() : field->GetBlockRevision();
      any = true;
    }
    recorded += any;
  }
  return recorded;
}

bool UBlockWorld::StoredLightInputsStillMatch(
    const std::shared_ptr<const RelightComputeResult> &context,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog) const
{
  ++LightInputStats.requests;
  if (!catalog || !context->input_catalog)
    return false;
  // An arbitrary callback may change without touching voxel residency. It is
  // deliberately never cached unless its owner supplies a revision contract.
  // Legacy contexts that explicitly read light take the full validation path;
  // output field writes are irrelevant only to geometry-derived contexts.
  if (!KnownAirEvidenceVersionQuery || !context->retained_inputs_geometry_only)
  {
    ++LightInputStats.dependency_validations;
    return ValidateRelightInputs(*context, *this, catalog) ==
           RelightInputValidation::Current;
  }
  const auto evidence = KnownAirEvidenceVersionQuery();
  const auto revision = Chunks.GetGeometryInputRevision();
  const auto &p = VerticalProfile;
  const auto matches = [&](const KnownAirEvidenceVersion &e)
  {
    const auto &m = LightInputMemo;
    return m.initialized &&
           m.input_revision == Chunks.GetGeometryInputRevision() &&
           m.evidence_producers == e.producers && m.min_y == e.min_y &&
           m.max_y == e.max_y && m.folder == e.folder && m.catalog == catalog &&
           m.lighting_configuration == LightingConfigurationIdentity &&
           m.profile.MinY == p.MinY && m.profile.MaxY == p.MaxY &&
           m.profile.SeaLevelY == p.SeaLevelY &&
           m.profile.BedrockTopY == p.BedrockTopY &&
           m.profile.DatumY == p.DatumY;
  };
  auto &memo = LightInputMemo;
  if (!matches(evidence))
  {
    ++LightInputStats.epoch_resets;
    memo.entries.clear();
    memo.initialized = true;
    memo.input_revision = revision;
    memo.evidence_producers = evidence.producers;
    memo.min_y = evidence.min_y;
    memo.max_y = evidence.max_y;
    memo.folder = std::string(evidence.folder);
    memo.profile = p;
    memo.lighting_configuration = LightingConfigurationIdentity;
    memo.catalog = catalog;
  }
  const auto found = memo.entries.find(context.get());
  if (found != memo.entries.end() && found->second.input.lock() == context &&
      found->second.input_identity == Chunks.GetGeometryInputRevisionIdentity())
  {
    ++LightInputStats.memo_hits;
    return found->second.current;
  }
  ++LightInputStats.dependency_validations;
  const bool current = ValidateRelightInputs(*context, *this, catalog) ==
                       RelightInputValidation::Current;
  // Filling a terrain/disk index during validation can itself publish new
  // evidence. Do not memoize a result across that publication boundary.
  if (!matches(KnownAirEvidenceVersionQuery()))
  {
    memo.initialized = false;
    memo.entries.clear();
    return current;
  }
  // Each resident section can own two independent domain contexts. A fixed
  // 1024-entry clear made larger stable censuses evict their own working set
  // and walk every dependency again. Bound history beyond the resident set,
  // retiring expired receipts without discarding all still-live validation.
  const auto capacity = Chunks.GetSectionLightFieldCount() * 2 + 1024;
  if (memo.entries.size() >= capacity)
  {
    for (auto it = memo.entries.begin(); it != memo.entries.end();)
      if (it->second.input.expired())
        it = memo.entries.erase(it);
      else
        ++it;
  }
  // Unexpected externally retained contexts cannot grow this optional cache.
  // The uncached result remains truthful and all revision checks still apply.
  if (memo.entries.size() < capacity)
    memo.entries[context.get()] = {
        context, Chunks.GetGeometryInputRevisionIdentity(), current};
  return current;
}

std::shared_ptr<const RelightComputeResult>
UBlockWorld::GetRecordedLightInputContext(glm::ivec3 coord,
                                          LightDomain domain) const
{
  const auto field = Chunks.GetSectionLightField(coord);
  if (!field)
    return {};
  const auto &proof = field->LightProofs[domain == LightDomain::Sky ? 0 : 1];
  return proof.owner == LightCalculationOwner ? proof.input : nullptr;
}

LightCalculationState UBlockWorld::GetLightCalculationState(
    glm::ivec3 coord, LightDomain domain,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog) const
{
  const auto field = Chunks.GetSectionLightField(coord);
  if (!field)
    return LightCalculationState::Missing;
  const auto &proof = field->LightProofs[domain == LightDomain::Sky ? 0 : 1];
  if (!proof.input || proof.owner != LightCalculationOwner ||
      proof.incarnation != field->GetIncarnation() ||
      proof.field_revision != (domain == LightDomain::Sky
                                   ? field->GetSkyRevision()
                                   : field->GetBlockRevision()) ||
      !StoredLightInputsStillMatch(proof.input, current_catalog))
    return LightCalculationState::Missing;
  // Every input was known at record time and the full read set still matches.
  // Rechecking the same known stamps a second time adds no evidence.
  return proof.input->inputs_complete ? LightCalculationState::Complete
                                      : LightCalculationState::Transient;
}

bool UBlockWorld::LightCalculationsStillCurrent(
    const std::vector<glm::ivec3> &sections, bool sky, bool block,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog,
    bool require_padded) const
{
  std::unordered_map<const RelightComputeResult *,
                     std::shared_ptr<const RelightComputeResult>>
      contexts;
  bool any = false;
  for (const auto coord : sections)
  {
    const auto field = Chunks.GetSectionLightField(coord);
    if (!field)
      return false;
    any = true;
    for (int domain = 0; domain < 2; ++domain)
    {
      if (!(domain == 0 ? sky : block))
        continue;
      const auto &proof = field->LightProofs[domain];
      if (!proof.input || proof.owner != LightCalculationOwner ||
          proof.incarnation != field->GetIncarnation() ||
          proof.field_revision != (domain == 0 ? field->GetSkyRevision()
                                               : field->GetBlockRevision()))
        return false;
      if (require_padded && (!proof.padded_installed ||
                             !(field->PaddedDomains & (domain == 0 ? 1 : 2))))
        return false;
      contexts.emplace(proof.input.get(), proof.input);
    }
  }
  if (!any || (!sky && !block))
    return false;
  for (const auto &context : contexts)
    if (!StoredLightInputsStillMatch(context.second, current_catalog))
      return false;
  return true;
}

bool UBlockWorld::HasCompleteLightCalculation(
    glm::ivec3 coord,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog) const
{
  if (GetLightCalculationState(coord, LightDomain::Sky, current_catalog) !=
      LightCalculationState::Complete)
    return false;
  const auto field = Chunks.GetSectionLightField(coord);
  const auto &sky = field->LightProofs[0];
  const auto &block = field->LightProofs[1];
  if (block.input == sky.input && block.owner == LightCalculationOwner &&
      block.incarnation == field->GetIncarnation() &&
      block.field_revision == field->GetBlockRevision())
    return true;
  return GetLightCalculationState(coord, LightDomain::Block, current_catalog) ==
         LightCalculationState::Complete;
}

bool UBlockWorld::HasCompleteLightSamplingCalculation(
    glm::ivec3 coord,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog) const
{
  const auto field = Chunks.GetSectionLightField(coord);
  if (!field || !field->PaddedLight || field->PaddedDomains != 3 ||
      !field->LightProofs[0].padded_installed ||
      !field->LightProofs[1].padded_installed)
    return false;
  return HasCompleteLightCalculation(coord, catalog);
}

bool UBlockWorld::HasRecordedLightSamplingOutput(glm::ivec3 coord) const
{
  const auto field = Chunks.GetSectionLightField(coord);
  if (!field || !field->GetPaddedData() || field->GetPaddedDomains() != 3)
    return false;
  for (int domain = 0; domain < 2; ++domain)
  {
    const auto &proof = field->LightProofs[domain];
    const auto &context = proof.input;
    if (!context || !context->inputs_complete ||
        !context->content_certificate || proof.owner != LightCalculationOwner ||
        proof.incarnation != field->GetIncarnation() ||
        proof.field_revision != (domain == 0 ? field->GetSkyRevision()
                                             : field->GetBlockRevision()) ||
        !proof.padded_installed ||
        !context->content_certificate->MatchesInputs(
            context->read_set, context->input_profile,
            context->input_lighting_configuration, context->input_catalog,
            context->include_skylight, context->include_block_light) ||
        !context->content_certificate->MatchesOutput(
            coord, *field->GetPaddedData(), domain))
      return false;
  }
  return true;
}

std::shared_ptr<const SectionLightVolume>
UBlockWorld::GetCurrentLightSamplingVolume(glm::ivec3 coord) const
{
  if (!FluidDefinitions || !HasCompleteLightSamplingCalculation(
                               coord, FluidDefinitions->GetCatalogSnapshot()))
    return nullptr;
  return Chunks.GetSectionLightField(coord)->GetPaddedData();
}

SectionGeometryRenderInputs UBlockWorld::CaptureGeometryRenderInputs(
    glm::ivec3 coord,
    std::shared_ptr<const BlockDefinitionCatalog> catalog) const
{
  static constexpr glm::ivec3 offsets[] = {{0, 0, 0},  {-1, 0, 0}, {1, 0, 0},
                                           {0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                           {0, 0, 1}};
  std::array<ChunkInputStamp, 7> inputs;
  for (size_t n = 0; n < inputs.size(); ++n)
    inputs[n] = CaptureInputStamp(coord + offsets[n], false, true);
  return SectionGeometryRenderInputs::FromStamps(
      coord, Chunks.GetGeometryInputRevisionIdentity(), VerticalProfile,
      std::move(catalog), inputs);
}

SectionGeometryRenderInputs
UBlockWorld::CaptureCurrentGeometryRenderInputs(glm::ivec3 coord) const
{
  if (!FluidDefinitions)
    return {};
  const auto catalog = FluidDefinitions->GetCatalogSnapshot();
  auto inputs = CaptureGeometryRenderInputs(coord, catalog);
  return FluidDefinitions->GetCatalogSnapshot() == catalog
             ? std::move(inputs)
             : SectionGeometryRenderInputs{};
}

std::optional<SectionRenderLightSnapshot>
UBlockWorld::CaptureRenderLightSnapshot(glm::ivec3 coord) const
{
  if (!FluidDefinitions)
    return std::nullopt;
  const auto catalog = FluidDefinitions->GetCatalogSnapshot();
  if (!HasCompleteLightSamplingCalculation(coord, catalog))
    return std::nullopt;
  const auto field = Chunks.GetSectionLightField(coord);
  const auto volume = field->GetPaddedData();
  SectionRenderLightSnapshot snapshot;
  snapshot.Geometry = CaptureGeometryRenderInputs(coord, catalog);
  if (!snapshot.Geometry.valid ||
      FluidDefinitions->GetCatalogSnapshot() != catalog)
    return std::nullopt;
  snapshot.Data = volume;
  snapshot.Incarnation = field->GetIncarnation();
  snapshot.Revision = field->GetRevision();
  return snapshot;
}

SceneLightSample UBlockWorld::SampleSceneLight(glm::ivec3 cell,
                                               SceneLightPolicy policy) const
{
  if (policy == SceneLightPolicy::Flat)
    return {SceneLightState::FlatConstant, PackLight(15, 0)};
  if (VerticalProfile.IsValid() && !VerticalProfile.ContainsY(cell.y))
    return {SceneLightState::BoundaryConstant,
            PackLight(cell.y > VerticalProfile.MaxY ? 15 : 0, 0)};

  const auto coord = UChunkManager::WorldToChunk(cell);
  const auto field = Chunks.GetSectionLightField(coord);
  if (!field)
    return {};
  const auto volume = GetCurrentLightSamplingVolume(coord);
  if (!volume)
    return {SceneLightState::Transient, std::nullopt, field->GetIncarnation(),
            field->GetRevision()};
  const auto local = cell - coord * CHUNK_SIZE;
  return {SceneLightState::Computed,
          volume->cells[SectionLightVolume::Index(local.x, local.y, local.z)],
          field->GetIncarnation(), field->GetRevision()};
}

bool UBlockWorld::HasKnownOpenSkyAbsentProof(glm::ivec3 coord) const
{
  if (KnownAirColumnFloorQuery)
  {
    const auto floor = KnownAirColumnFloorQuery({coord.x, 0, coord.z});
    return (floor && coord.y >= *floor) ||
           (KnownAirInternalSliceQuery && KnownAirInternalSliceQuery(coord));
  }
  return KnownOpenSkyAbsentQuery && KnownOpenSkyAbsentQuery(coord);
}

bool UBlockWorld::IsKnownAbsentAirChunk(glm::ivec3 coord,
                                        InputStampValidationBatch *batch) const
{
  if (Chunks.GetChunk(coord))
    return false;
  if (Chunks.HasLoadedAirSlice(coord))
    return true;
  if (coord.y < VerticalProfile.MinChunkY(CHUNK_SIZE) ||
      coord.y > VerticalProfile.MaxChunkY(CHUNK_SIZE))
    return true;
  if (!batch || !KnownAirColumnFloorQuery)
    return HasKnownOpenSkyAbsentProof(coord);
  if (batch->owner != this)
  {
    batch->air_floors.clear();
    batch->owner = this;
  }
  const glm::ivec3 column(coord.x, 0, coord.z);
  auto it = batch->air_floors.find(column);
  if (it == batch->air_floors.end())
    it = batch->air_floors.emplace(column, KnownAirColumnFloorQuery(column))
             .first;
  return (it->second && coord.y >= *it->second) ||
         (KnownAirInternalSliceQuery && KnownAirInternalSliceQuery(coord));
}

ChunkInputStamp UBlockWorld::CaptureInputStamp(glm::ivec3 coord,
                                               bool reads_light,
                                               bool reads_content) const
{
  const UChunk *chunk = Chunks.GetChunk(coord);
  const auto field =
      reads_light && !chunk ? Chunks.GetSectionLightField(coord) : nullptr;
  auto stamp = ChunkInputStamp::Capture(coord, chunk, reads_light,
                                        reads_content, field.get());
  if (!chunk && (reads_light || reads_content))
    stamp.missingEvidence = IsKnownAbsentAirChunk(coord)
                                ? MissingChunkEvidence::KnownAir
                                : MissingChunkEvidence::Unknown;
  return stamp;
}

bool UBlockWorld::InputStampStillMatches(const ChunkInputStamp &stamp,
                                         InputStampValidationBatch *batch) const
{
  const UChunk *chunk = Chunks.GetChunk(stamp.coord);
  const auto field = stamp.readsLight && !chunk
                         ? Chunks.GetSectionLightField(stamp.coord)
                         : nullptr;
  if (!stamp.Matches(chunk, field.get()))
    return false;
  if (stamp.missingEvidence == MissingChunkEvidence::Untracked)
    return true;
  return !chunk &&
         IsKnownAbsentAirChunk(stamp.coord, batch) ==
             (stamp.missingEvidence == MissingChunkEvidence::KnownAir);
}

bool UBlockWorld::InputStampLightDomainsStillMatch(
    const ChunkInputStamp &stamp, bool reads_sky, bool reads_block,
    InputStampValidationBatch *batch) const
{
  auto geometry = stamp;
  geometry.readsLight = false;
  if (!InputStampStillMatches(geometry, batch))
    return false;
  if (!stamp.readsLight || (!reads_sky && !reads_block))
    return true;
  const UChunk *chunk = Chunks.GetChunk(stamp.coord);
  const auto field = Chunks.GetSectionLightField(stamp.coord);
  return stamp.incarnation == (chunk ? chunk->GetIncarnation() : 0) &&
         stamp.lightIncarnation == (field ? field->GetIncarnation() : 0) &&
         (!reads_sky ||
          stamp.skyLight == (field ? field->GetSkyRevision() : 0)) &&
         (!reads_block ||
          stamp.blockLight == (field ? field->GetBlockRevision() : 0));
}

BlockId UBlockWorld::GetBlock(glm::ivec3 pos) const
{
  return Chunks.GetBlock(pos);
}

BlockQueryResult UBlockWorld::QueryBlock(glm::ivec3 pos) const
{
  return Chunks.QueryBlock(pos);
}

FluidCellState UBlockWorld::GetFluidState(glm::ivec3 pos) const
{
  return Chunks.GetFluidState(pos);
}

void UBlockWorld::SetBlock(glm::ivec3 pos, BlockId Id)
{
  if (Id == BLOCK_AIR)
  {
    Chunks.SetBlock(pos, BLOCK_AIR);
    Chunks.ClearFluidState(pos);
    if (CaptureBuffer)
    {
      CaptureBuffer->SetBlock(pos, BLOCK_AIR);
    }
    return;
  }
  Chunks.SetBlock(pos, Id);
  if (FluidDefinitions != nullptr)
  {
    if (const BlockDefinition *def = FluidDefinitions->GetByIdFast(Id))
    {
      if (def->Physics.IsLiquid)
      {
        const FluidKind kind = FluidKindFromDefinition(def);
        Chunks.SetFluidState(pos, FluidCellState::Source().WithKind(kind));
      }
      else if (!IsFluidPermeableFromDefinition(Id, def,
                                               /*is_liquid=*/false))
      {
        // W6.2 (water audit E-3): a solid placed into water annihilates the
        // water — the stale fluid byte under the solid must not persist to
        // disk and resurrect when the block is later replaced by decor.
        Chunks.ClearFluidState(pos);
      }
    }
  }
  if (CaptureBuffer)
  {
    CaptureBuffer->SetBlock(pos, Id);
    CaptureBuffer->SetFluidPacked(pos,
                                  PackFluidCellState(Chunks.GetFluidState(pos)));
  }
}

void UBlockWorld::SetBlockInChunk(UChunk &chunk, glm::ivec3 pos, BlockId Id)
{
  const glm::ivec3 local = UChunkManager::WorldToLocal(pos);
  if (Id == BLOCK_AIR)
  {
    chunk.SetBlockLocal(local, BLOCK_AIR);
    chunk.ClearFluidLocal(local);
    if (CaptureBuffer)
    {
      CaptureBuffer->SetBlock(pos, BLOCK_AIR);
    }
    return;
  }
  chunk.SetBlockLocal(local, Id);
  if (FluidDefinitions != nullptr)
  {
    if (const BlockDefinition *def = FluidDefinitions->GetByIdFast(Id))
    {
      if (def->Physics.IsLiquid)
      {
        const FluidKind kind = FluidKindFromDefinition(def);
        chunk.SetFluidLocal(local, FluidCellState::Source().WithKind(kind));
      }
      else if (!IsFluidPermeableFromDefinition(Id, def,
                                               /*is_liquid=*/false))
      {
        // W6.2: clear the stale fluid byte under solids (see SetBlock).
        chunk.ClearFluidLocal(local);
      }
    }
  }
  if (CaptureBuffer)
  {
    CaptureBuffer->SetBlock(pos, Id);
    CaptureBuffer->SetFluidPacked(
        pos, PackFluidCellState(chunk.GetFluidLocal(local)));
  }
}

void UBlockWorld::SetFluidState(glm::ivec3 pos, FluidCellState state)
{
  Chunks.SetFluidState(pos, state);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, PackFluidCellState(state));
  }
}

void UBlockWorld::SetFluidStateInChunk(UChunk &chunk, glm::ivec3 pos,
                                       FluidCellState state)
{
  const glm::ivec3 local = UChunkManager::WorldToLocal(pos);
  chunk.SetFluidLocal(local, state);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, PackFluidCellState(state));
  }
}

void UBlockWorld::ClearFluidState(glm::ivec3 pos)
{
  Chunks.ClearFluidState(pos);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, 0);
  }
}

bool UBlockWorld::IsAir(glm::ivec3 pos) const
{
  return GetBlock(pos) == BLOCK_AIR;
}

void UBlockWorld::Clear()
{
  Chunks.Clear();
  LightInputMemo = {};
  LightInputStats = {};
  LightCalculationOwner = std::make_shared<const int>(0);
}

size_t UBlockWorld::CountNonAir() const
{
  size_t count = 0;
  ForEachBlock([&count](glm::ivec3, BlockId) { ++count; });
  return count;
}

bool UBlockWorld::HasAnyNonAir() const
{
  bool found = false;
  ForEachBlock([&found](glm::ivec3, BlockId id)
               {
                 if (id != BLOCK_AIR)
                 {
                   found = true;
                 }
               });
  return found;
}

void UBlockWorld::ForEachBlock(
    const std::function<void(glm::ivec3, BlockId)> &fn) const
{
  Chunks.ForEachBlock(fn);
}

} // namespace cutum
