#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Lighting/SectionLightRestoreQueue.h"
#include "World/Math/BlockTypes.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cutum
{
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

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

static void TestColumnLightPersistence(const std::filesystem::path &folder_path)
{
  using namespace cutum;
  const auto folder = folder_path.string();
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone, torch;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  torch.Name = "torch";
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  definitions->ReplaceAll({{8, stone}, {50, torch}},
                          {{"stone", 8}, {"torch", 50}});
  UBlockRegistry registry(nullptr, definitions);
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  const glm::ivec3 root(2, -3, -3), upper(2, -2, -3);
  const glm::ivec3 column(root.x, 0, root.z);
  const auto source_pos = root * CHUNK_SIZE + glm::ivec3(16, 8, 8);
  UBlockWorld original;
  original.SetLightingConfigurationIdentity(1);
  original.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  original.SetBlock({root.x * CHUNK_SIZE, -64, root.z * CHUNK_SIZE}, 8);
  original.SetBlock(source_pos, 50);
  for (const auto coord : {root, upper})
  {
    RelightJobSpec spec;
    spec.min_world_y = -64;
    spec.max_world_y = 319;
    spec.include_known_air_outputs = true;
    const auto computed =
        UChunkRelightSnapshot::CaptureSections(original, spec, {coord})
            .Compute(registry);
    Expect(computed.inputs_complete && computed.chunks.size() == 1,
           "column cache fixture computes independent padded AIR fields");
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        coord, *computed.chunks[0].padded_light, true, true);
    Expect(original.RecordLightCalculation(computed, catalog) == 1,
           "column cache fixture records actual calculation");
  }
  UChunkStorageService storage;
  const auto snapshot = storage.CaptureColumnLight(column, original, registry);
  Expect(snapshot->size() == 2 && !original.GetChunkManager().HasChunk(root),
         "column snapshot includes field-only AIR above terrain without "
         "geometry allocation");
  definitions->ReplaceAll(catalog->ById, {{"stone", 8}, {"torch", 50}});
  registry.Reload();
  const auto refreshed_catalog = registry.GetDefinitionsCatalogSnapshot();
  ColumnLightCaptureStats capture_stats;
  storage.ObserveLightCapture =
      [&](glm::ivec3 observed, const ColumnLightCaptureStats &stats)
  {
    Expect(observed == column,
           "export observer retains actual column identity");
    capture_stats = stats;
  };
  const auto refreshed_snapshot =
      storage.CaptureColumnLight(column, original, registry);
  Expect(refreshed_catalog != catalog && refreshed_snapshot->size() == 2 &&
             capture_stats.current == 0 &&
             capture_stats.retained_current == 2 &&
             capture_stats.equivalent_catalog == 2 &&
             capture_stats.exported == 2 &&
             (*refreshed_snapshot)[0]->volume.cells ==
                 (*snapshot)[0]->volume.cells,
         "real registry refresh exports current retained proofs and diagnoses "
         "catalog-only refusal");
  storage.ObserveLightCapture = {};
  const auto evidence_before = storage.GetKnownAirEvidenceRevision();
  Expect(storage.WriteColumnLight(folder, column, *snapshot) &&
             storage.GetKnownAirEvidenceRevision() == evidence_before &&
             storage.GetHighestChunkSliceOnDisk(folder, column) ==
                 kNoChunkSliceOnDisk,
         "light metadata never creates terrain or alters its absence index");
  UAsyncChunkIO io;
  std::filesystem::remove(
      UChunkStorageService::ColumnLightFilePath(folder, column));
  io.RequestSaveColumnLight(column, snapshot, storage, folder);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "actual metadata write worker completes");
  const auto saves = io.DrainSaves();
  Expect(
      saves.size() == 1 && saves[0].success && saves[0].cleanupOnly &&
          saves[0].cleanupOperation == "column_light_receipts" &&
          storage.GetHighestChunkSliceOnDisk(folder, column) ==
              kNoChunkSliceOnDisk,
      "async metadata write completes separately from terrain slice indexing");
  const ChunkGenerationToken token{column, 7};
  const auto cancellation = std::make_shared<std::atomic<bool>>(false);
  io.RequestLoad(root, storage, registry, folder, token, cancellation, 42,
                 true);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "actual metadata-only load worker completes");
  auto loads = io.DrainLoads();
  Expect(loads.size() == 1 && loads[0].format == ChunkDiskFormat::Absent &&
             !loads[0].success && loads[0].columnLight &&
             loads[0].columnLight->size() == 2 &&
             loads[0].token.sequence == 7 && loads[0].demandId == 42,
         "absent terrain fast path still delivers optional receipts under its "
         "exact load owner");
  cancellation->store(true);
  io.RequestLoad(root, storage, registry, folder, token, cancellation, 43,
                 true);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)) && io.DrainLoads().empty(),
         "cancelled async metadata load cannot produce a completion");
  cancellation->store(false);
  std::filesystem::remove(
      UChunkStorageService::ColumnLightFilePath(folder, column));
  storage.SaveTerrainColumn(column, original, folder, registry, 319, -64);
  storage.SaveTerrainColumn({root.x + 1, 0, root.z}, original, folder, registry,
                            319, -64);
  Expect(storage.GetHighestChunkSliceOnDisk(folder, column) == -4,
         "ordinary save preserves AIR receipts above highest geometry slice");
  io.RequestLoad({root.x, -4, root.z}, storage, registry, folder, token,
                 cancellation, 44, true);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "joined terrain and receipt load worker completes");
  loads = io.DrainLoads();
  Expect(loads.size() == 1 && loads[0].success && loads[0].decodedBuffer &&
             loads[0].columnLight && loads[0].columnLight->size() == 2,
         "actual binary slice load carries decoded terrain and column receipts "
         "together");
  UChunkStorageService reopened;
  const auto loaded = reopened.ReadColumnLight(folder, column);
  Expect(loaded && loaded->size() == 2 &&
             (*loaded)[0]->volume.cells == (*snapshot)[0]->volume.cells,
         "ordinary column save has reloadable content receipts and all padded "
         "bytes");
  UBlockWorld cold;
  cold.SetLightingConfigurationIdentity(1);
  cold.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return false; });
  reopened.LoadTerrainColumn({root.x + 1, 0, root.z}, cold, folder, registry,
                             319, -64);
  reopened.LoadTerrainColumn(column, cold, folder, registry, 319, -64);
  Expect(!cold.HasCompleteLightSamplingCalculation(root, catalog),
         "terrain load with unknown dependencies cannot certify disk light");
  SectionLightRestoreQueue queue;
  for (const auto &receipt : *loaded)
    Expect(queue.Offer(receipt, 7),
           "decoded column receipts obtain bounded pending owners");
  unsigned ready = 0;
  const auto owner_current = [](glm::ivec3, uint64_t owner)
  { return owner == 7; };
  const auto on_ready = [&](glm::ivec3) { ++ready; };
  auto stats = queue.Drain(cold, catalog, owner_current, on_ready, 1);
  Expect(stats.waiting == 1 && ready == 0 && queue.Size() == 2,
         "unknown disk receipt retains its original owner without granting "
         "readiness");
  cold.SetKnownOpenSkyAbsentQuery(
      [&](glm::ivec3 coord)
      {
        return coord.y != -4 &&
               reopened.IsChunkSliceAbsentOnDisk(folder, coord);
      });
  stats = queue.Drain(cold, catalog, owner_current, on_ready, 1);
  Expect(
      stats.restored == 1 && ready == 1 && queue.Size() == 1 &&
          cold.HasCompleteLightSamplingCalculation(upper, catalog),
      "waiting receipt cannot starve a ready AIR field under one-visit budget");
  cold.SetKnownOpenSkyAbsentQuery(
      [&](glm::ivec3 coord)
      { return reopened.IsChunkSliceAbsentOnDisk(folder, coord); });
  stats = queue.Drain(cold, catalog, owner_current, on_ready, 1);
  Expect(stats.restored == 1 && queue.Size() == 0 &&
             queue.RetainedBytes() == 0 &&
             cold.HasCompleteLightSamplingCalculation(root, catalog) &&
             !cold.GetChunkManager().HasChunk(root) &&
             cold.GetChunkManager()
                     .GetSectionLightField(root)
                     ->GetPaddedData()
                     ->cells == (*loaded)[0]->volume.cells,
         "late known geometry restores exact saved AIR field and retires "
         "bounded owner");
  auto obsolete = std::make_shared<PersistedSectionLight>(*(*loaded)[0]);
  obsolete->volume.cells[0] ^= 1;
  Expect(queue.Offer(obsolete, 7),
         "obsolete disk payload may wait for physical validation");
  stats = queue.Drain(cold, catalog, owner_current, [](glm::ivec3) {});
  Expect(stats.current == 1 && stats.restored == 0 &&
             cold.GetChunkManager()
                     .GetSectionLightField(root)
                     ->GetPaddedData()
                     ->cells == (*loaded)[0]->volume.cells,
         "current physical calculation supersedes disk cache without "
         "overwriting its field");
  cold.SetBlock(source_pos, BLOCK_AIR);
  Expect(queue.Offer((*loaded)[0], 7),
         "stale cache receipt can be queued for validation");
  stats = queue.Drain(cold, catalog, owner_current, on_ready);
  Expect(stats.rejected == 1 && queue.Size() == 0 && ready == 2,
         "changed source retires disk cache owner without satisfying real "
         "relight debt");
  cold.SetBlock(source_pos, 50);
  Expect(queue.Offer((*loaded)[0], 6),
         "old generation receipt admitted only as pending cache");
  stats = queue.Drain(cold, catalog, owner_current, on_ready);
  Expect(stats.cancelled == 1 && ready == 2 && queue.RetainedBytes() == 0,
         "stale load generation cannot restore or signal a field");
  Expect(queue.Offer((*loaded)[0], 6) && queue.Offer((*loaded)[0], 7) &&
             queue.Size() == 1,
         "new load generation replaces duplicate coordinate without leaking "
         "owners");
  queue.CancelColumn(column);
  Expect(queue.Size() == 0 && queue.RetainedBytes() == 0,
         "column cancellation retires every retained disk receipt");
  for (size_t n = 0; n < SectionLightRestoreQueue::MaxCount; ++n)
  {
    auto receipt = std::make_shared<PersistedSectionLight>(*(*loaded)[0]);
    receipt->coord.y = int(n);
    Expect(queue.Offer(std::move(receipt), 7),
           "bounded receipt queue fills its admitted slots");
  }
  auto extra = std::make_shared<PersistedSectionLight>(*(*loaded)[0]);
  extra->coord.y = 999;
  Expect(
      !queue.Offer(extra, 7) &&
          queue.Size() == SectionLightRestoreQueue::MaxCount &&
          queue.RetainedBytes() <= SectionLightRestoreQueue::MaxBytes,
      "receipt queue refuses capacity overflow without losing existing owners");
  queue.Clear();
  extra->signature.reserve(SectionLightRestoreQueue::MaxBytes + 1);
  Expect(!queue.Offer(extra, 7) && queue.Size() == 0 &&
             queue.RetainedBytes() == 0,
         "receipt queue refuses an oversized payload before retaining it");
  const auto metadata =
      UChunkStorageService::ColumnLightFilePath(folder, column);
  {
    std::ofstream out(metadata, std::ios::binary | std::ios::trunc);
    out << "{broken";
  }
  Expect(!reopened.ReadColumnLight(folder, column),
         "corrupt optional metadata falls back to real relight");
  Expect(reopened.WriteColumnLight(folder, column, *snapshot),
         "valid receipt cache can replace corruption");
  std::ifstream valid_file(metadata, std::ios::binary);
  std::vector<uint8_t> oversized(std::istreambuf_iterator<char>(valid_file),
                                 {});
  valid_file.close();
  oversized.resize(kMaxColumnLightBytes + 1, ' ');
  Expect(reopened.WriteBytesAtomically(metadata, oversized) &&
             !reopened.ReadColumnLight(folder, column),
         "column receipt file reads are bounded before JSON parsing");
  Expect(reopened.WriteColumnLight(folder, column, *snapshot) &&
             reopened.RemoveTerrainColumnFromDisk(folder, column, 319, nullptr,
                                                  -64) &&
             !std::filesystem::exists(metadata),
         "full terrain purge removes stale column receipts too");
  Expect(std::filesystem::weakly_canonical(folder_path).parent_path() ==
             std::filesystem::weakly_canonical(
                 std::filesystem::temp_directory_path()),
         "receipt test cleanup stays within its explicit temporary world");
  std::filesystem::remove_all(folder_path);
}

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
  TestColumnLightPersistence(world_folder.string() + "_receipts");

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
