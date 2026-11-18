#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Math/BlockTypes.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{

void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "negative_height_storage_test: " << message << std::endl;
    std::exit(1);
  }
}

} // namespace

int main()
{
  using namespace cutum;
  constexpr int kMinY = -64;
  constexpr int kMaxY = 319;
  constexpr BlockId kStone = 8;
  const auto temp_root = std::filesystem::temp_directory_path();
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto world_folder =
      temp_root / ("cubatarium_negative_height_storage_" +
                   std::to_string(nonce));

  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UChunkStorageService storage;
  UBlockWorld source;
  const glm::ivec3 ground(2, 0, -3);
  source.SetBlock(glm::ivec3(ground.x * CHUNK_SIZE + 4, kMinY,
                             ground.z * CHUNK_SIZE + 7),
                  kStone);
  source.SetBlock(glm::ivec3(ground.x * CHUNK_SIZE + 4, kMaxY,
                             ground.z * CHUNK_SIZE + 7),
                  kStone);

  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  Expect(storage.SaveChunk(
             {ground.x, -4, ground.z},
             *source.GetChunkManager().GetChunk({ground.x, -4, ground.z}),
             world_folder.string(), registry),
         "lower fixture slice is saved independently");
  Expect(
      storage.GetHighestChunkSliceOnDisk(world_folder.string(), ground) == -4,
      "lower-only disk index provides known-air evidence above its signed top");
  // Reproduce the native failure: adding an upper island raises the column
  // maximum while an independently absent negative section remains empty.
  UBlockWorld sparse;
  sparse.SetFluidDefinitions(definitions.get());
  bool terrain_complete = true;
  const auto folder = world_folder.string();
  sparse.SetKnownAirColumnFloorQuery(
      [&](glm::ivec3 column) -> std::optional<int>
      {
        if (!terrain_complete || storage.IsColumnSavePending(column))
          return {};
        return std::max(
                   GetHighestNonAirChunkSlice(sparse, column, kMaxY, kMinY),
                   storage.GetHighestChunkSliceOnDisk(folder, column)) +
               1;
      },
      [&]()
      {
        UBlockWorld::KnownAirEvidenceVersion version;
        version.producers[0] = terrain_complete ? 1 : 0;
        version.producers[2] = storage.GetKnownAirEvidenceIdentity();
        version.producers[3] = storage.GetKnownAirEvidenceRevision();
        version.folder = folder;
        return version;
      },
      [&](glm::ivec3 coord)
      {
        return terrain_complete &&
               !storage.IsColumnSavePending({coord.x, 0, coord.z}) &&
               storage.IsChunkSliceAbsentOnDisk(folder, coord);
      });
  const glm::ivec3 internal(ground.x, -3, ground.z);
  sparse.SetBlock({ground.x * CHUNK_SIZE + 4, kMinY, ground.z * CHUNK_SIZE + 7},
                  kStone);
  RelightJobSpec sparse_spec;
  sparse_spec.min_world_y = -48;
  sparse_spec.max_world_y = -33;
  sparse_spec.include_known_air_outputs = true;
  const auto install_sparse_calculation = [&]()
  {
    const auto computed =
        UChunkRelightSnapshot::CaptureSections(sparse, sparse_spec, {internal})
            .Compute(registry);
    Expect(computed.inputs_complete && computed.unknown_input_chunks == 0 &&
               computed.chunks.size() == 1,
           "exact absence closes every physical halo and roof dependency");
    InstallComputedLight(sparse, computed.chunks[0], true, true);
    Expect(sparse.RecordLightCalculation(computed, catalog) == 1 &&
               sparse.HasCompleteLightSamplingCalculation(internal, catalog),
           "actual computed padded AIR field obtains current physical proof");
  };
  install_sparse_calculation();
  const auto originally_empty = sparse.CaptureInputStamp(internal, false, true);
  Expect(originally_empty.missingEvidence == MissingChunkEvidence::KnownAir,
         "lower-only column proves its empty negative internal section");
  sparse.SetBlock({ground.x * CHUNK_SIZE, 48, ground.z * CHUNK_SIZE}, kStone);
  UBlockWorld::InputStampValidationBatch validation_batch;
  Expect(sparse.IsKnownAbsentAirChunk(internal) &&
             sparse.InputStampStillMatches(originally_empty) &&
             sparse.InputStampStillMatches(originally_empty, &validation_batch),
         "upper island must not turn independently empty negative section "
         "unknown");
  Expect(!sparse.HasCompleteLightSamplingCalculation(internal, catalog),
         "upper geometry mutation still invalidates its real physical inputs");
  install_sparse_calculation();
  terrain_complete = false;
  Expect(!sparse.IsKnownAbsentAirChunk(internal),
         "exact disk absence cannot certify an incomplete terrain producer");
  terrain_complete = true;
  storage.MarkColumnSavePending(ground);
  Expect(!sparse.InputStampStillMatches(originally_empty),
         "exact absence cannot bypass a pending column write");
  storage.ClearColumnSavePending(ground);
  UChunk saved_internal(internal);
  saved_internal.SetBlockLocal({1, 1, 1}, kStone);
  Expect(storage.SaveChunk(internal, saved_internal, folder, registry) &&
             !sparse.InputStampStillMatches(originally_empty) &&
             !storage.IsChunkSliceAbsentOnDisk(folder, internal),
         "unloaded persisted internal data is never classified as AIR");
  Expect(storage.RemoveChunkSliceFromDisk(folder, internal) &&
             sparse.InputStampStillMatches(originally_empty),
         "exact index observes signed slice removal");
  UChunk saved_roof({ground.x, 3, ground.z});
  saved_roof.SetBlockLocal({1, 1, 1}, kStone);
  Expect(storage.SaveChunk({ground.x, 3, ground.z}, saved_roof, folder,
                           registry) &&
             sparse.InputStampStillMatches(originally_empty),
         "higher disk occupancy does not erase independent internal absence");
  Expect(storage.SaveChunk(internal, saved_internal, folder, registry) &&
             !storage.IsChunkSliceAbsentOnDisk(folder, internal),
         "lower internal saved slice enters exact membership below disk top");
  Expect(storage.RemoveChunkSliceFromDisk(folder, internal) &&
             storage.GetHighestChunkSliceOnDisk(folder, ground) == 3 &&
             storage.IsChunkSliceAbsentOnDisk(folder, internal),
         "removing a non-highest slice refreshes exact membership too");
  UChunkStorageService reopened;
  Expect(
      reopened.IsChunkSliceAbsentOnDisk(folder, internal) &&
          !reopened.IsChunkSliceAbsentOnDisk(folder, {ground.x, 3, ground.z}),
      "cold complete index distinguishes an internal gap from a saved roof");
  Expect(storage.RemoveChunkSliceFromDisk(folder, {ground.x, 3, ground.z}),
         "remove temporary sparse upper fixture before legacy coverage");
  UBlockWorld probe;
  const glm::ivec3 probe_coord(ground.x, -3, ground.z);
  probe.GetChunkManager().EnsureChunk(probe_coord);
  const auto fixture_folder = world_folder.string();
  probe.SetKnownAirColumnFloorQuery(
      [&](glm::ivec3 column) -> std::optional<int>
      {
        if (storage.IsColumnSavePending(column))
          return {};
        const int top =
            storage.GetHighestChunkSliceOnDisk(fixture_folder, column);
        return top == kNoChunkSliceOnDisk ? -4 : top + 1;
      },
      [&]()
      {
        UBlockWorld::KnownAirEvidenceVersion v;
        v.producers[2] = storage.GetKnownAirEvidenceIdentity();
        v.producers[3] = storage.GetKnownAirEvidenceRevision();
        v.folder = fixture_folder;
        v.min_y = kMinY;
        v.max_y = kMaxY;
        return v;
      });
  RelightJobSpec spec;
  spec.min_world_y = kMinY;
  spec.max_world_y = kMaxY;
  spec.include_block_light = false;
  auto result =
      UChunkRelightSnapshot::CaptureSections(probe, spec, {probe_coord})
          .Compute(registry);
  InstallComputedLight(*probe.GetChunkManager().GetChunk(probe_coord),
                       result.chunks[0].light_packed, true, false);
  probe.RecordLightCalculation(result, catalog);
  Expect(
      probe.GetLightCalculationState(probe_coord, LightDomain::Sky, catalog) ==
              LightCalculationState::Transient &&
          probe.LightCalculationsStillCurrent({probe_coord}, true, false,
                                              catalog),
      "unknown lower halo remains transient while upper disk evidence is "
      "tracked");
  const auto probe_revision = probe.GetChunkManager().GetInputRevision();
  storage.MarkColumnSavePending(ground);
  Expect(probe.GetChunkManager().GetInputRevision() == probe_revision &&
             probe.GetLightCalculationState(probe_coord, LightDomain::Sky,
                                            catalog) ==
                 LightCalculationState::Missing,
         "actual storage save-pending transition invalidates memo without "
         "voxel writes");
  const auto pending_revision = storage.GetKnownAirEvidenceRevision();
  storage.MarkColumnSavePending(ground);
  Expect(storage.GetKnownAirEvidenceRevision() == pending_revision,
         "duplicate save-pending notice does not churn evidence identity");
  storage.ClearColumnSavePending(ground);
  Expect(
      probe.GetLightCalculationState(probe_coord, LightDomain::Sky, catalog) ==
          LightCalculationState::Transient,
      "save completion restores exact lower-only disk evidence");

  storage.SaveTerrainColumn(ground, source, world_folder.string(), registry,
                            kMaxY, kMinY);
  Expect(storage.GetHighestChunkSliceOnDisk(world_folder.string(), ground) == 19,
         "disk index includes the top slice for the expanded world range");

  Expect(probe.GetChunkManager().GetInputRevision() == probe_revision &&
             probe.GetLightCalculationState(probe_coord, LightDomain::Sky,
                                            catalog) ==
                 LightCalculationState::Missing,
         "saving a higher real disk slice immediately invalidates old "
         "known-air proof");
  UChunkStorageService replacement_storage;
  Expect(replacement_storage.GetKnownAirEvidenceIdentity() !=
             storage.GetKnownAirEvidenceIdentity(),
         "replacement storage cannot alias an old producer revision");
  UBlockWorld restored;
  const int loaded = storage.LoadTerrainColumn(
      ground, restored, world_folder.string(), registry, kMaxY, kMinY);
  Expect(loaded > 0, "column loader restores the saved signed range");
  Expect(restored.GetBlock(glm::ivec3(ground.x * CHUNK_SIZE + 4, kMinY,
                                     ground.z * CHUNK_SIZE + 7)) == kStone,
         "save/load preserves a block at Y=-64");
  Expect(restored.GetBlock(glm::ivec3(ground.x * CHUNK_SIZE + 4, kMaxY,
                                     ground.z * CHUNK_SIZE + 7)) == kStone,
         "save/load preserves a block at Y=319");
  Expect(restored.GetChunkManager().HasChunk(glm::ivec3(ground.x, -4,
                                                        ground.z)) &&
             restored.GetChunkManager().HasChunk(glm::ivec3(ground.x, 19,
                                                             ground.z)),
         "restored column materializes both endpoint chunk slices");

  std::string remove_error;
  Expect(storage.RemoveTerrainColumnFromDisk(world_folder.string(), ground,
                                              kMaxY, &remove_error, kMinY),
         "signed terrain-column cleanup succeeds");
  Expect(storage.GetHighestChunkSliceOnDisk(world_folder.string(), ground) ==
             kNoChunkSliceOnDisk,
         "cleanup removes negative and positive disk slices");

  result = UChunkRelightSnapshot::CaptureSections(probe, spec, {probe_coord})
               .Compute(registry);
  InstallComputedLight(*probe.GetChunkManager().GetChunk(probe_coord),
                       result.chunks[0].light_packed, true, false);
  probe.RecordLightCalculation(result, catalog);
  Expect(
      probe.GetLightCalculationState(probe_coord, LightDomain::Sky, catalog) ==
              LightCalculationState::Complete &&
          !probe.HasCompleteLightCalculation(probe_coord, catalog),
      "real disk removal admits a fresh known sky proof but does not certify "
      "an uncomputed block channel");
  const auto normalized_root = std::filesystem::weakly_canonical(temp_root);
  const auto normalized_world = std::filesystem::weakly_canonical(world_folder);
  Expect(normalized_world.parent_path() == normalized_root,
         "temporary world remains directly inside the system temp directory");
  std::filesystem::remove_all(normalized_world);

  std::cout << "negative_height_storage_test: OK" << std::endl;
  return 0;
}
