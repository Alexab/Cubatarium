#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/ColumnSaveReceipt.h"
#include "World/IO/DiskAirCancellationProbe.h"
#include "World/IO/DiskReadFailureProbe.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Lighting/SectionLightRestoreQueue.h"
#include "World/Math/BlockTypes.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
  BlockDefinition stone, torch, water;
  stone.Name = "stone";
  stone.Physics = BlockPhysicsProfile::Solid();
  torch.Name = "torch";
  torch.Render.Style = BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  water.Name = "water";
  water.Physics.IsLiquid = true;
  water.Physics.FluidKindPreset = FluidKind::Water;
  water.Render.Style = BlockRenderStyle::Fluid;
  definitions->ReplaceAll({{8, stone}, {50, torch}, {9, water}},
                          {{"stone", 8}, {"torch", 50}, {"water", 9}});
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
  const glm::ivec3 fluid_pos(root.x * CHUNK_SIZE + 2, -63, root.z * CHUNK_SIZE + 2);
  const auto fluid_state = FluidCellState::Flowing(3, true).WithKind(FluidKind::Water);
  original.SetBlock(fluid_pos, 9);
  original.SetFluidState(fluid_pos, fluid_state);
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
  definitions->ReplaceAll(catalog->ById, {{"stone", 8}, {"torch", 50}, {"water", 9}});
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
             capture_stats.exported == 2 && capture_stats.frozen_context == 2 &&
             capture_stats.recorded_output == 2 &&
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
  // S3.05: real disk -> fresh world -> rejected cache -> physical recalculate.
  for (const std::string change : {"compatible", "algorithm", "materials",
                                   "profile", "configuration", "inputs", "legacy"})
  {
    auto case_definitions = std::make_shared<UBlockDefinitionStorage>();
    auto case_blocks = catalog->ById;
    if (change == "materials") case_blocks.at(50).Lighting.Emission = 6;
    case_definitions->ReplaceAll(case_blocks, {{"stone", 8}, {"torch", 50}, {"water", 9}});
    UBlockRegistry case_registry(nullptr, case_definitions);
    const auto case_catalog = case_registry.GetDefinitionsCatalogSnapshot();
    PersistedColumnLight disk_payload = *snapshot;
    if (change == "algorithm")
      for (auto &entry : disk_payload)
      {
        auto older = std::make_shared<PersistedSectionLight>(*entry);
        older->signature = "light-v5" + older->signature.substr(older->signature.find(':'));
        older->checksum = persisted_light_detail::Checksum(*older);
        entry = std::move(older);
      }
    Expect(reopened.WriteColumnLight(folder, column, disk_payload),
           "compatibility matrix writes physical column cache");
    if (change == "legacy")
      std::filesystem::remove(UChunkStorageService::ColumnLightFilePath(folder, column));
    UBlockWorld case_world;
    case_world.SetFluidDefinitions(case_definitions.get());
    case_world.SetLightingConfigurationIdentity(change == "configuration" ? 2 : 1);
    auto case_profile = case_world.GetVerticalProfile();
    if (change == "profile") ++case_profile.SeaLevelY;
    case_world.SetVerticalProfile(case_profile);
    case_world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return false; });
    Expect(reopened.LoadTerrainColumn({root.x + 1, 0, root.z}, case_world,
               folder, case_registry, 319, -64) > 0 &&
               reopened.LoadTerrainColumn(column, case_world, folder,
                   case_registry, 319, -64) > 0 &&
               case_world.GetBlock(source_pos) == 50 &&
               case_world.GetBlock({root.x * CHUNK_SIZE, -64, root.z * CHUNK_SIZE}) == 8,
           "every cache compatibility case cold-loads terrain edits unchanged");
    if (change == "inputs") case_world.SetBlock(source_pos, BLOCK_AIR);
    case_world.SetKnownOpenSkyAbsentQuery([&](glm::ivec3 coord) {
      return reopened.IsChunkSliceAbsentOnDisk(folder, coord);
    });
    const auto disk_receipts = reopened.ReadColumnLight(folder, column);
    SectionLightRestoreQueue case_queue;
    if (disk_receipts)
      for (const auto &entry : *disk_receipts)
        Expect(case_queue.Offer(entry, 1), "disk cache obtains restore owner");
    const auto case_stats = case_queue.Drain(case_world, case_catalog,
        [](glm::ivec3, uint64_t owner) { return owner == 1; }, [](glm::ivec3) {}, 8);
    if (change == "compatible")
      Expect(case_stats.restored == 2 && case_world.HasCompleteLightSamplingCalculation(root, case_catalog) &&
                 case_world.GetChunkManager().GetSectionLightField(root)->GetPaddedData()->cells ==
                     (*snapshot)[0]->volume.cells,
             "compatible cold cache restores all padded bytes under fresh identities");
    else
    {
      Expect(!case_world.HasCompleteLightSamplingCalculation(root, case_catalog) &&
                 !case_world.GetChunkManager().GetSectionLightField(root) &&
                 (change == "legacy" ? !disk_receipts : case_stats.rejected == 2),
             "incompatible or legacy cold cache cannot grant field readiness");
      RelightJobSpec repair;
      repair.min_world_y = -64;
      repair.max_world_y = 319;
      repair.include_known_air_outputs = true;
      auto result = UChunkRelightSnapshot::CaptureSections(case_world, repair, {root}).Compute(case_registry);
      Expect(result.inputs_complete && result.chunks.size() == 1,
             "cold incompatible cache has complete recapture inputs");
      InstallComputedLight(case_world, result.chunks.front(), true, true);
      Expect(case_world.RecordLightCalculation(result, case_catalog) == 1 &&
                 case_world.HasCompleteLightSamplingCalculation(root, case_catalog),
             "incompatible or legacy cold cache is replaced by certified calculation");
    }
    const auto volume = case_world.GetChunkManager().GetSectionLightField(root)->GetPaddedData();
    const int expected_block = change == "inputs" ? 0 : change == "materials" ? 5 : 13;
    Expect(volume && UnpackBlock(volume->cells[SectionLightVolume::Index(15, 8, 8)]) == expected_block &&
               case_world.GetBlock(source_pos) == (change == "inputs" ? BLOCK_AIR : 50) &&
               case_world.GetBlock(fluid_pos) == 9 &&
               PackFluidCellState(case_world.GetFluidState(fluid_pos)) == PackFluidCellState(fluid_state) &&
               case_world.GetBlock({root.x * CHUNK_SIZE, -64, root.z * CHUNK_SIZE}) == 8,
           "cold cache recovery preserves terrain and computes current emission/removal");
    std::cout << "persisted_disk_matrix case=" << change << " PASS block="
              << expected_block << " fluid=" << int(PackFluidCellState(fluid_state)) << '\n';
  }
  Expect(reopened.WriteColumnLight(folder, column, *snapshot),
         "compatibility matrix restores its valid isolated disk fixture");
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
  Expect(stats.rejected == 1 && queue.Size() == 0 && ready == 2 &&
             stats.reasons[static_cast<size_t>(
                 SectionLightRestoreFailure::ContentMismatch)] == 1 &&
             stats.first_rejected && stats.first_rejected->first == root &&
             stats.first_rejected->second.reason ==
                 SectionLightRestoreFailure::ContentMismatch &&
             stats.first_rejected->second.has_input &&
             stats.first_rejected->second.current_known &&
             stats.first_rejected->second.input_coord ==
                 UChunkManager::WorldToChunk(source_pos),
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

static void TestAirOnlyColumnStorage(const std::filesystem::path &root_folder)
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  for (auto format : {ChunkWriteFormat::Binary, ChunkWriteFormat::Json})
  {
    const auto folder =
        root_folder.string() +
        (format == ChunkWriteFormat::Binary ? "_binary" : "_json");
    Expect(!std::filesystem::exists(folder),
           "AIR-only storage fixture never reuses a world");
    UChunkStorageService storage({format, true});
    UBlockWorld source;
    source.SetFluidDefinitions(definitions.get());
    const glm::ivec3 column(-7, 0, 5);
    UBinaryChunkSerializer codec;
    for (int cy = -4; cy <= 19; ++cy)
    {
      const glm::ivec3 coord(column.x, cy, column.z);
      UChunk empty(coord);
      empty.SetWeatherState(.25f, 123.125);
      const auto bytes = codec.Serialize(coord, empty, registry);
      const auto decoded = codec.Deserialize(bytes.bytes, coord, registry);
      Expect(ApplyTerrainSliceLoad(source, coord, &decoded, nullptr,
                                   {column, 9}, 9, false),
             "all-air fixture installs real decoded full frames");
    }
    Expect(
        IsTerrainChunkComplete(source, column, 319, kNoTerrainChunkSlice, -64),
        "full explicit AIR profile is complete before saving");
    storage.SaveTerrainColumn(column, source, folder, registry, 319, -64);
    Expect(
        storage.GetHighestChunkSliceOnDisk(folder, column) == 19,
        "new AIR-only column persists metadata even without a non-air ceiling");
    for (int slices : {1, 12, 23})
    {
      UAsyncChunkIO io;
      UBlockWorld partial;
      TerrainSliceLoadOwnership owner;
      const ChunkGenerationToken token{column, 17};
      const auto cancellation = std::make_shared<std::atomic<bool>>(false);
      for (int cy = -4; cy < -4 + slices; ++cy)
        io.RequestLoad({column.x, cy, column.z}, storage, registry, folder,
                       token, cancellation, 83);
      Expect(io.WaitIdleFor(std::chrono::seconds(5)),
             "partial full-profile AIR disk workers finish");
      const auto loads = io.DrainLoads();
      Expect(loads.size() == static_cast<size_t>(slices),
             "actual disk results cover requested partial AIR range");
      for (const auto &load : loads)
        Expect(load.success && load.decodedBuffer && load.demandId == 83 &&
                   load.cancellation == cancellation &&
                   ApplyTerrainSliceLoad(partial, load.coord,
                                         load.decodedBuffer.get(), nullptr,
                                         load.token, 17, false, &owner),
               "actual Binary/JSON results commit under their matching demand");
      Expect(!IsTerrainChunkComplete(partial, column, 319, kNoTerrainChunkSlice,
                                     -64),
             "partial decoded AIR data cannot authorize whole-column save");
      cancellation->store(true, std::memory_order_release);
      const auto retired = owner.RetireAir(partial.GetChunkManager());
      Expect(retired.size() == static_cast<size_t>(slices) &&
                 partial.GetChunkManager().GetResidentTerrainColumnCount() ==
                     0 &&
                 partial.GetChunkManager().GetSectionLightFieldCount() == 0 &&
                 storage.GetHighestChunkSliceOnDisk(folder, column) == 19,
             "all owned partial AIR retires with no resident fields or disk "
             "purge");
      for (const auto &load : loads)
        Expect(!ApplyTerrainSliceLoad(
                   partial, load.coord, load.decodedBuffer.get(), nullptr,
                   load.token, 17,
                   cancellation->load(std::memory_order_acquire), &owner),
               "late decoded result cannot republish cancelled data");
    }
    UBlockWorld cold;
    Expect(storage.LoadTerrainColumn(column, cold, folder, registry, 319, -64) >
                   0 &&
               cold.GetChunkManager().GetResidentChunkCount() == 0 &&
               IsTerrainChunkComplete(cold, column, 319, kNoTerrainChunkSlice,
                                      -64),
           "cold AIR-only disk load retains complete data without geometry");
    for (int cy = -4; cy <= 19; ++cy)
    {
      const auto snapshot = cold.GetChunkManager().MakeTerrainSaveSnapshot(
          {column.x, cy, column.z});
      Expect(snapshot &&
                 std::abs(snapshot->GetSurfaceWetness() - .25f) < .00002f &&
                 snapshot->GetWeatherLastUpdateSec() == 123.125,
             "both legacy formats retain every AIR slice's weather");
    }
    const auto edited = glm::ivec3(column.x, -4, column.z) * CHUNK_SIZE;
    cold.SetBlock(edited, 8);
    Expect(IsTerrainChunkComplete(cold, column, 319, kNoTerrainChunkSlice, -64),
           "local edit retains decoded full-profile authority");
    storage.SaveTerrainColumn(column, cold, folder, registry, 319, -64);
    UBlockWorld recold;
    Expect(storage.LoadTerrainColumn(column, recold, folder, registry, 319,
                                     -64) > 0 &&
               recold.GetBlock(edited) == 8 &&
               IsTerrainChunkComplete(recold, column, 319, kNoTerrainChunkSlice,
                                      -64),
           "edited all-air base survives another cold load without being "
           "classified as incomplete");
    // A single decoded top frame plus indexed absences is not an explicit
    // complete profile and must not bypass legacy partial-generation guards.
    UBlockWorld sparse;
    Expect(
        storage.RemoveTerrainColumnFromDisk(folder, column, 319, nullptr, -64),
        "sparse fixture removes the previous full saved range");
    const glm::ivec3 top(column.x, 19, column.z);
    const auto top_snapshot =
        source.GetChunkManager().MakeTerrainSaveSnapshot(top);
    Expect(top_snapshot &&
               storage.SaveChunk(top, *top_snapshot, folder, registry),
           "sparse fixture retains only one valid explicit top frame");
    Expect(
        storage.LoadTerrainColumn(column, sparse, folder, registry, 319, -64) >
                0 &&
            !IsTerrainChunkComplete(sparse, column, 319, kNoTerrainChunkSlice,
                                    -64),
        "indexed absent AIR does not mint explicit whole-profile completeness");
    sparse.GetChunkManager().MarkTerrainColumnReadUnresolved(column);
    Expect(storage.LoadTerrainColumn(column, sparse, folder, registry, 319,
                                     -64) < 0 &&
               sparse.GetChunkManager().HasUnresolvedTerrainColumnRead(column),
           "a valid but still incomplete sparse profile cannot release the "
           "synchronous save barrier");
    const auto normalized = std::filesystem::weakly_canonical(folder);
    Expect(normalized.parent_path() ==
               std::filesystem::weakly_canonical(
                   std::filesystem::temp_directory_path()),
           "all-air storage cleanup stays in its unique system-temp world");
    std::filesystem::remove_all(normalized);
  }
}

static void TestAirTerrainIo(const std::filesystem::path &folder)
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UChunkStorageService storage;
  UBlockWorld source;
  const glm::ivec3 column(-2, 0, 3), air_coord(-2, -3, 3), roof(-2, -2, 3);
  const auto cell = air_coord * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  for (int x = 0; x < CHUNK_SIZE; ++x)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      source.SetBlock(column * CHUNK_SIZE + glm::ivec3(x, -64, z), 8);
  source.SetBlock(roof * CHUNK_SIZE, 8);
  UChunk air_chunk(air_coord);
  std::array<uint8_t, CHUNK_VOLUME> pattern{};
  for (size_t i = 0; i < pattern.size(); ++i)
    pattern[i] = static_cast<uint8_t>(i * 19 + 31);
  air_chunk.ReplaceLightData(pattern);
  air_chunk.SetWeatherState(0.75f, 71.125);
  UBinaryChunkSerializer fixture_codec;
  const auto fixture_air = fixture_codec.Deserialize(
      fixture_codec.Serialize(air_coord, air_chunk, registry).bytes,
      air_coord, registry);
  Expect(ApplyTerrainSliceLoad(source, air_coord, &fixture_air, nullptr,
                              {column, 1}, 1, false),
         "dense fixture declares its internal AIR instead of saving unknown data");
  storage.SaveTerrainColumn(column, source, folder.string(), registry, -17,
                            -64);
  Expect(storage.SaveChunk(air_coord, air_chunk, folder.string(), registry),
         "dense legacy AIR fixture writes real CCHK4 payload");
  UBlockWorld restored;
  Expect(storage.LoadTerrainColumn(column, restored, folder.string(), registry,
                                   -17, -64) > 0 &&
             restored.GetChunkManager().GetResidentChunkCount() == 2 &&
             restored.GetChunkManager().HasLoadedAirSlice(air_coord) &&
             !restored.GetChunkManager().HasChunk(air_coord) &&
             IsTerrainChunkComplete(restored, column, -17, -2, -64),
         "real dense disk column completes with geometry-free AIR");
  auto snapshot = restored.GetChunkManager().MakeTerrainSaveSnapshot(air_coord);
  Expect(snapshot && snapshot->GetLightData() == pattern &&
             std::abs(snapshot->GetSurfaceWetness() - 0.75f) < 0.00002f &&
             snapshot->GetWeatherLastUpdateSec() == 71.125,
         "dense disk AIR retains canonical light and weather");
  const auto first_field =
      restored.GetChunkManager().GetSectionLightField(air_coord);
  MaterializeRequiredTerrainColumnSlices(restored, column, -17, -2, -64);
  Expect(!restored.GetChunkManager().HasChunk(air_coord),
         "legacy materialization helper respects explicit loaded AIR");
  UAsyncChunkIO io;
  const ChunkGenerationToken token{column, 12};
  const auto cancellation = std::make_shared<std::atomic<bool>>(false);
  io.RequestLoad(air_coord, storage, registry, folder.string(), token,
                 cancellation, 71);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "actual dense AIR load worker completes");
  auto loads = io.DrainLoads();
  Expect(loads.size() == 1 && loads[0].success && loads[0].decodedBuffer,
         "dense worker returns a decoded AIR payload");
  io.RequestLoad(air_coord, storage, registry, folder.string(), token,
                 cancellation, 70, false, true);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "injected actual read worker completes");
  auto invalid_loads = io.DrainLoads();
  UBlockWorld invalid_world;
  Expect(
      invalid_loads.size() == 1 && invalid_loads[0].success &&
          invalid_loads[0].injectedInvalidFrame &&
          invalid_loads[0].decodedBuffer &&
          !invalid_loads[0].decodedBuffer->HasDecodedTerrainFrame(air_coord) &&
          !ApplyTerrainSliceLoad(invalid_world, air_coord,
                                 invalid_loads[0].decodedBuffer.get(), nullptr,
                                 token, 12, false) &&
          invalid_world.GetChunkManager().GetResidentTerrainColumnCount() == 0,
      "real codec rejects corrupted read buffer without publishing data");
  UBlockWorld async_world;
  UChunkBuffer failed_decode;
  failed_decode.SetChunkLightData(air_coord, pattern);
  Expect(!ApplyTerrainSliceLoad(async_world, air_coord, &failed_decode, nullptr,
                                token, 12, false),
         "unvalidated metadata-only buffer is not a successful terrain load");
  Expect(!ApplyTerrainSliceLoad(async_world, air_coord,
                                loads[0].decodedBuffer.get(), nullptr, token,
                                13, false) &&
             !ApplyTerrainSliceLoad(async_world, air_coord,
                                    loads[0].decodedBuffer.get(), nullptr,
                                    token, 12, true) &&
             async_world.GetChunkManager().GetSectionLightFieldCount() == 0,
         "real worker payload cannot commit under stale or cancelled owner");
  Expect(ApplyTerrainSliceLoad(async_world, air_coord,
                               loads[0].decodedBuffer.get(), nullptr, token, 12,
                               false) &&
             !async_world.GetChunkManager().HasChunk(air_coord),
         "actual async commit installs AIR without geometry");
  // A partial column can never authorize the whole-column save/purge path.
  // Its cancelled disk demand must instead release exactly its own AIR data.
  UBlockWorld cancelled_partial;
  TerrainSliceLoadOwnership partial_owner;
  Expect(ApplyTerrainSliceLoad(cancelled_partial, air_coord,
                               loads[0].decodedBuffer.get(), nullptr, token, 12,
                               false, &partial_owner),
         "partial demand installs a real decoded worker AIR frame");
  auto &partial_manager = cancelled_partial.GetChunkManager();
  const auto retained_field = partial_manager.GetSectionLightField(air_coord);
  const auto retired = partial_owner.RetireAir(partial_manager);
  Expect(retired == std::vector<glm::ivec3>{air_coord} &&
             partial_manager.QueryBlock(cell).IsUnloaded() &&
             !partial_manager.HasDecodedTerrainSlice(air_coord) &&
             !partial_manager.GetSectionLightField(air_coord) &&
             partial_manager.GetResidentTerrainColumnCount() == 0 &&
             retained_field->GetData() == pattern,
         "cancelled partial disk demand retires owned AIR without save/purge; "
         "external field snapshot remains immutable");
  Expect(partial_owner.RetireAir(partial_manager).empty(),
         "cancelled demand cannot retire the same AIR twice");
  Expect(!ApplyTerrainSliceLoad(cancelled_partial, air_coord,
                                loads[0].decodedBuffer.get(), nullptr, token,
                                12, true, &partial_owner) &&
             !partial_manager.HasLoadedTerrainSlice(air_coord),
         "already decoded late result cannot recreate cancelled AIR");
  UBlockWorld cancellation_cold;
  Expect(
      storage.LoadChunk(air_coord, cancellation_cold, folder.string(),
                        registry) == 0 &&
          cancellation_cold.GetChunkManager().HasLoadedAirSlice(air_coord) &&
          cancellation_cold.GetChunkManager()
                  .MakeTerrainSaveSnapshot(air_coord)
                  ->GetWeatherLastUpdateSec() == 71.125,
      "partial cancellation never purges the saved AIR frame or its weather");
  const auto apply_owned =
      [&](UBlockWorld &target, TerrainSliceLoadOwnership &owner)
  {
    return ApplyTerrainSliceLoad(target, air_coord,
                                 loads[0].decodedBuffer.get(), nullptr, token,
                                 12, false, &owner);
  };
  UBlockWorld duplicates;
  TerrainSliceLoadOwnership first_owner, duplicate_owner;
  Expect(apply_owned(duplicates, first_owner) &&
             apply_owned(duplicates, duplicate_owner) &&
             duplicate_owner.RetireAir(duplicates.GetChunkManager()).empty() &&
             duplicates.GetChunkManager().HasLoadedAirSlice(air_coord) &&
             first_owner.RetireAir(duplicates.GetChunkManager()).size() == 1,
         "duplicate completion cannot claim or retire pre-existing AIR data");
  UBlockWorld replaced;
  TerrainSliceLoadOwnership old_owner, new_owner;
  Expect(apply_owned(replaced, old_owner), "old demand installs AIR");
  const auto old_field =
      replaced.GetChunkManager().GetSectionLightField(air_coord);
  replaced.GetChunkManager().RemoveChunk(air_coord);
  Expect(apply_owned(replaced, new_owner) &&
             replaced.GetChunkManager()
                     .GetSectionLightField(air_coord)
                     ->GetIncarnation() != old_field->GetIncarnation() &&
             old_owner.RetireAir(replaced.GetChunkManager()).empty() &&
             replaced.GetChunkManager().HasDecodedTerrainSlice(air_coord) &&
             new_owner.RetireAir(replaced.GetChunkManager()).size() == 1,
         "cancelled old demand cannot retire a newer AIR incarnation at the "
         "same coordinate");
  UBlockWorld original_manager, foreign_manager;
  TerrainSliceLoadOwnership original_lease, foreign_lease;
  Expect(
      apply_owned(original_manager, original_lease) &&
          apply_owned(foreign_manager, foreign_lease) &&
          original_lease.RetireAir(foreign_manager.GetChunkManager()).empty() &&
          foreign_manager.GetChunkManager().HasLoadedAirSlice(air_coord) &&
          original_lease.RetireAir(original_manager.GetChunkManager()).size() ==
              1,
      "manager identity protects replacement and scratch worlds without "
      "consuming the actual owner");
  UBlockWorld edited_demand;
  TerrainSliceLoadOwnership edit_lease;
  Expect(apply_owned(edited_demand, edit_lease),
         "editable demand installs AIR");
  edited_demand.SetBlock(cell, 8);
  edited_demand.SetBlock(cell, BLOCK_AIR);
  Expect(
      edit_lease.RetireAir(edited_demand.GetChunkManager()).empty() &&
          edited_demand.GetChunkManager().HasChunk(air_coord) &&
          edited_demand.GetChunkManager().HasDecodedTerrainSlice(air_coord) &&
          edited_demand.GetChunkManager()
                  .MakeTerrainSaveSnapshot(air_coord)
                  ->GetWeatherLastUpdateSec() == 71.125,
      "materialized edits retain data/weather/decoded authority even after "
      "editing back to AIR");
  UBlockWorld mixed_demand;
  TerrainSliceLoadOwnership mixed_lease;
  Expect(apply_owned(mixed_demand, mixed_lease), "mixed demand installs AIR");
  mixed_demand.SetBlock(roof * CHUNK_SIZE, 8);
  Expect(mixed_lease.RetireAir(mixed_demand.GetChunkManager()).empty() &&
             mixed_demand.GetChunkManager().HasLoadedAirSlice(air_coord) &&
             mixed_demand.GetBlock(roof * CHUNK_SIZE) == 8,
         "a column with geometry retains AIR until normal save/eviction, "
         "avoiding incomplete purge");
  UBlockWorld field_only;
  TerrainSliceLoadOwnership field_lease;
  const auto field_before_install =
      field_only.GetChunkManager().EnsureSectionLightField(air_coord);
  Expect(apply_owned(field_only, field_lease) &&
             field_only.GetChunkManager().GetSectionLightField(air_coord) !=
                 field_before_install &&
             field_lease.RetireAir(field_only.GetChunkManager()).size() == 1 &&
             !field_only.GetChunkManager().GetSectionLightField(air_coord),
         "field-only allocation cannot claim terrain; cancellation releases "
         "only newly installed field ownership");
  const auto clock = async_world.GetChunkManager().GetInputRevision();
  io.RequestSave(air_coord, storage, folder.string(), async_world, registry,
                 token);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)),
         "actual AIR save worker completes");
  auto saves = io.DrainSaves();
  Expect(saves.size() == 1 && saves[0].success &&
             async_world.GetChunkManager().GetInputRevision() == clock &&
             async_world.GetChunkManager().GetResidentChunkCount() == 0,
         "real async save uses detached AIR snapshot without live mutations");
  UBlockWorld cold;
  Expect(storage.LoadChunk(air_coord, cold, folder.string(), registry) == 0 &&
             cold.GetChunkManager()
                     .MakeTerrainSaveSnapshot(air_coord)
                     ->GetLightData() == pattern,
         "cold disk reload retains saved geometry-free AIR light");
  io.RequestSave(air_coord + glm::ivec3(0, 1, 0), storage, folder.string(),
                 async_world, registry, token);
  saves = io.DrainSaves();
  Expect(
      saves.size() == 1 && !saves[0].success &&
          saves[0].error == "chunk_missing_before_serialize",
      "async save refuses unknown slices instead of creating empty geometry");
  UBlockWorld edited;
  edited.SetBlock(cell, 8);
  Expect(ApplyTerrainSliceLoad(edited, air_coord, loads[0].decodedBuffer.get(),
                               nullptr, token, 12, false) &&
             edited.GetBlock(cell) == 8,
         "late dense AIR completion preserves resident edit");
  ClearTerrainColumnChunks(restored, column, -17, -64);
  Expect(
      !restored.GetChunkManager().HasLoadedTerrainSlice(air_coord) &&
          restored.QueryBlock(cell).IsUnloaded() &&
          !restored.GetChunkManager().GetSectionLightField(air_coord),
      "normal column eviction retires geometry-free AIR knowledge and bytes");
  Expect(storage.LoadTerrainColumn(column, restored, folder.string(), registry,
                                   -17, -64) > 0 &&
             restored.GetChunkManager()
                     .GetSectionLightField(air_coord)
                     ->GetIncarnation() != first_field->GetIncarnation(),
         "return from real disk starts fresh AIR field ownership");
  Expect(storage.RemoveChunkSliceFromDisk(folder.string(), air_coord),
         "sparse fixture removes real internal AIR file");
  Expect(!storage.ReadKnownAbsentTerrainSlice(folder.string(),
                                              {column.x + 1, -3, column.z}) &&
             !storage.ReadKnownAbsentTerrainSlice(folder.string(),
                                                  {column.x, -1, column.z}),
         "absence outside saved column bounds cannot invent terrain");
  storage.MarkColumnSavePending(column);
  Expect(!storage.ReadKnownAbsentTerrainSlice(folder.string(), air_coord),
         "pending save blocks sparse absence authority");
  storage.ClearColumnSavePending(column);
  const auto absent =
      storage.ReadKnownAbsentTerrainSlice(folder.string(), air_coord);
  Expect(absent.has_value(),
         "exact internal saved-column absence is explicit AIR evidence");
  UBlockWorld sparse;
  Expect(storage.LoadTerrainColumn(column, sparse, folder.string(), registry,
                                   -17, -64) > 0 &&
             sparse.GetChunkManager().HasLoadedAirSlice(air_coord) &&
             sparse.GetChunkManager().GetResidentChunkCount() == 2 &&
             IsTerrainChunkComplete(sparse, column, -17, -2, -64),
         "real sparse disk column completes without materializing its gap");
  io.RequestLoad(air_coord, storage, registry, folder.string(), token,
                 cancellation, 72);
  loads = io.DrainLoads();
  Expect(loads.size() == 1 && loads[0].format == ChunkDiskFormat::Absent &&
             !loads[0].success,
         "real absent fast path remains distinct from successful decode");
  UBlockWorld absent_world;
  TerrainSliceLoadOwnership absent_owner;
  Expect(
      !ApplyTerrainSliceLoad(absent_world, air_coord, nullptr, &*absent, token,
                             13, false) &&
          ApplyTerrainSliceLoad(absent_world, air_coord, nullptr, &*absent,
                                token, 12, false, &absent_owner) &&
          absent_world.GetChunkManager().GetResidentChunkCount() == 0,
      "absent completion requires current token and never allocates geometry");
  Expect(absent_owner.RetireAir(absent_world.GetChunkManager()).size() == 1 &&
             !absent_world.GetChunkManager().HasLoadedTerrainSlice(air_coord),
         "indexed absent AIR has cancellable data ownership but no decoded "
         "completeness authority");
  UBlockWorld missing;
  missing.SetBlock({column.x * CHUNK_SIZE, -64, column.z * CHUNK_SIZE}, 8);
  missing.SetBlock(roof * CHUNK_SIZE, 8);
  Expect(!AreTerrainColumnSlicesLoaded(missing, column, -17, -2, -64),
         "unknown internal slice still prevents completion");
  storage.SaveTerrainColumn(column, sparse, folder.string(), registry, -17,
                            -64);
  Expect(!storage.ReadKnownAbsentTerrainSlice(folder.string(), air_coord) &&
             sparse.GetChunkManager().GetResidentChunkCount() == 2,
         "dense compatibility save writes AIR without creating live holder");
  UBlockWorld recold;
  Expect(storage.LoadTerrainColumn(column, recold, folder.string(), registry,
                                   -17, -64) > 0 &&
             recold.GetChunkManager().HasLoadedAirSlice(air_coord) &&
             recold.GetChunkManager().GetResidentChunkCount() == 2,
         "sparse-to-dense save and cold reload keep geometry absent");
  const auto normalized = std::filesystem::weakly_canonical(folder);
  Expect(normalized.parent_path() ==
             std::filesystem::weakly_canonical(
                 std::filesystem::temp_directory_path()),
         "AIR IO fixture remains inside system temp root");
  std::filesystem::remove_all(normalized);
}

static void TestDiskRetryReadSafety()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBinaryChunkSerializer binary;
  UJsonChunkSerializer json;
  const glm::ivec3 coord(-7, -3, 5), column(-7, 0, 5);
  const auto cell = coord * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  UChunk saved(coord);
  saved.SetBlockLocal({1, 2, 3}, 8);
  saved.SetWeatherState(.25f, 123.125);
  auto bytes = binary.Serialize(coord, saved, registry).bytes;
  bytes.push_back(
      0); // A complete-looking geometry payload with invalid framing.
  const auto invalid = binary.Deserialize(bytes, coord, registry);
  Expect(invalid.HasGeometryPayload(),
         "invalid binary regression retains parsed geometry");
  UBlockWorld rejected;
  Expect(!ApplyTerrainSliceLoad(rejected, coord, &invalid, nullptr, {column, 1},
                                1, false) &&
             !rejected.GetChunkManager().HasLoadedTerrainSlice(coord),
         "unsealed non-AIR disk frame must not mutate the world or become a "
         "successful load");
  const auto valid = binary.Deserialize(
      binary.Serialize(coord, saved, registry).bytes, coord, registry);
  UBlockWorld edited;
  Expect(ApplyTerrainSliceLoad(edited, coord, &valid, nullptr, {column, 1}, 1,
                               false),
         "valid disk frame establishes a complete base before edit");
  edited.SetBlock(cell, BLOCK_AIR);
  const auto incarnation =
      edited.GetChunkManager().GetChunk(coord)->GetIncarnation();
  edited.GetChunkManager().GetChunk(coord)->SetWeatherState(.75f, 456.125);
  Expect(
      ApplyTerrainSliceLoad(edited, coord, &valid, nullptr, {column, 2}, 2,
                            false) &&
          edited.GetBlock(cell) == BLOCK_AIR &&
          edited.GetChunkManager().GetChunk(coord)->GetIncarnation() ==
              incarnation &&
          edited.GetChunkManager().GetChunk(coord)->GetWeatherLastUpdateSec() ==
              456.125 &&
          edited.GetChunkManager().HasDecodedTerrainSlice(coord),
      "retry cannot replay stale non-AIR geometry/weather over local edits");
}

static void TestDiskRetryStorage()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBinaryChunkSerializer binary;
  const glm::ivec3 coord(-7, -3, 5), column(-7, 0, 5), prior(-7, -4, 5);
  const auto cell = coord * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  const auto edited_cell = prior * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  const auto read_file = [](const std::filesystem::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
  };
  const auto capture = [&](const std::filesystem::path &folder)
  {
    std::unordered_map<std::string, std::vector<uint8_t>> files;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(folder))
      if (entry.is_regular_file())
        files.emplace(entry.path().lexically_relative(folder).generic_string(),
                      read_file(entry.path()));
    return files;
  };
  for (auto format : {ChunkWriteFormat::Binary, ChunkWriteFormat::Json})
  {
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("cubatarium_disk_retry_safety_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Expect(!std::filesystem::exists(folder),
           "disk retry fixture must be fresh");
    UChunkStorageService storage({format, true});
    UBlockWorld source;
    for (int cy = -4; cy <= 19; ++cy)
    {
      const glm::ivec3 slice(column.x, cy, column.z);
      UChunk frame(slice);
      frame.SetWeatherState(.25f, 123.125);
      if (cy == -4 || cy == -3)
        frame.SetBlockLocal({1, 2, 3}, 8);
      const auto buffer = binary.Deserialize(
          binary.Serialize(slice, frame, registry).bytes, slice, registry);
      Expect(ApplyTerrainSliceLoad(source, slice, &buffer, nullptr, {column, 1},
                                   1, false),
             "retry fixture has24 explicit decoded frames");
    }
    storage.SaveTerrainColumn(column, source, folder.string(), registry, 319,
                              -64);
    const auto disk_format = format == ChunkWriteFormat::Binary
                                 ? ChunkDiskFormat::Binary
                                 : ChunkDiskFormat::Json;
    const auto damaged_path =
        storage.ChunkFilePath(folder.string(), coord, disk_format);
    const auto original = read_file(damaged_path);
    Expect(!original.empty(), "retry fixture has a real non-AIR frame");
    auto damaged = original;
    if (format == ChunkWriteFormat::Binary)
      damaged.push_back(0);
    else
    {
      std::string text(damaged.begin(), damaged.end());
      const std::string version = "\"format_version\":4";
      const auto offset = text.find(version);
      Expect(offset != std::string::npos, "JSON fixture version is explicit");
      text.replace(offset, version.size(), "\"format_version\":99");
      damaged.assign(text.begin(), text.end());
    }
    Expect(storage.WriteBytesAtomically(damaged_path, damaged),
           "inject one malformed complete-looking frame");
    const auto disk_before = capture(folder);
    UBlockWorld retry;
    Expect(storage.LoadTerrainColumn(column, retry, folder.string(), registry,
                                     319, -64) < 0 &&
               retry.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
               !retry.GetChunkManager().HasLoadedTerrainSlice(coord),
           "failed full-column read preserves valid earlier data and records "
           "unresolved status");
    retry.SetBlock(edited_cell, BLOCK_AIR);
    auto *edited_chunk = retry.GetChunkManager().GetChunk(prior);
    const auto edited_incarnation = edited_chunk->GetIncarnation();
    edited_chunk->SetWeatherState(.75f, 456.125);
    for (int attempt = 0; attempt < 5; ++attempt)
    {
      Expect(storage.LoadTerrainColumn(column, retry, folder.string(), registry,
                                       319, -64) < 0 &&
                 retry.GetBlock(edited_cell) == BLOCK_AIR &&
                 retry.GetChunkManager().GetChunk(prior)->GetIncarnation() ==
                     edited_incarnation &&
                 !IsTerrainChunkComplete(retry, column, 319,
                                         kNoTerrainChunkSlice, -64),
             "five failed retries cannot clear or overwrite local edits or "
             "claim completion");
      storage.SaveTerrainColumn(column, retry, folder.string(), registry, 319,
                                -64);
      Expect(capture(folder) == disk_before,
             "blocked save preserves every original/corrupt frame and light "
             "file byte-for-byte");
    }
    Expect(storage.WriteBytesAtomically(damaged_path, original),
           "restore the failed frame for real recovery");
    retry.GetChunkManager().EnsureChunk(coord);
    Expect(storage.LoadTerrainColumn(column, retry, folder.string(), registry,
                                     319, -64) < 0 &&
               retry.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
               !retry.GetChunkManager().HasDecodedTerrainSlice(coord),
           "allocated unvalidated data cannot be promoted to a recovered disk "
           "base");
    retry.GetChunkManager().RemoveChunk(coord);
    Expect(
        storage.LoadTerrainColumn(column, retry, folder.string(), registry, 319,
                                  -64) > 0 &&
            !retry.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
            IsTerrainChunkComplete(retry, column, 319, kNoTerrainChunkSlice,
                                   -64) &&
            retry.GetBlock(edited_cell) == BLOCK_AIR &&
            retry.GetChunkManager().GetChunk(prior)->GetIncarnation() ==
                edited_incarnation &&
            retry.GetChunkManager()
                    .GetChunk(prior)
                    ->GetWeatherLastUpdateSec() == 456.125,
        "recovery validates every frame and releases the barrier while "
        "retaining edits and weather");
    storage.SaveTerrainColumn(column, retry, folder.string(), registry, 319,
                              -64);
    UBlockWorld cold;
    Expect(storage.LoadTerrainColumn(column, cold, folder.string(), registry,
                                     319, -64) > 0 &&
               cold.GetBlock(edited_cell) == BLOCK_AIR &&
               cold.GetBlock(cell) == 8 &&
               cold.GetChunkManager()
                       .MakeTerrainSaveSnapshot(prior)
                       ->GetWeatherLastUpdateSec() == 456.125,
           "recovered edits survive save and cold disk reload in both formats");
    const auto resolved = std::filesystem::weakly_canonical(folder);
    Expect(resolved.parent_path() ==
               std::filesystem::weakly_canonical(
                   std::filesystem::temp_directory_path()),
           "retry fixture cleanup stays in its unique system-temp world");
    std::filesystem::remove_all(resolved);
  }
}

static void TestUnmarkedPartialSavePreservesDisk()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBinaryChunkSerializer codec;
  const glm::ivec3 column(-11, 0, 7), floor(-11, -4, 7);
  const auto deleted = floor * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  const auto added = deleted + glm::ivec3(1, 0, 0);
  const auto capture = [](const std::filesystem::path &folder)
  {
    std::map<std::string, std::vector<uint8_t>> files;
    if (std::filesystem::exists(folder))
      for (const auto &entry : std::filesystem::recursive_directory_iterator(folder))
        if (entry.is_regular_file())
        {
          std::ifstream input(entry.path(), std::ios::binary);
          files[entry.path().lexically_relative(folder).generic_string()] =
              std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
        }
    return files;
  };
  for (auto format : {ChunkWriteFormat::Binary, ChunkWriteFormat::Json})
  {
    const auto folder = std::filesystem::temp_directory_path() /
        ("cubatarium_partial_save_" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
    Expect(!std::filesystem::exists(folder), "partial save fixture is fresh");
    UChunkStorageService storage({format, true});
    UBlockWorld source, partial;
    for (int cy = -4; cy <= 19; ++cy)
    {
      const glm::ivec3 coord(column.x, cy, column.z);
      UChunk frame(coord);
      frame.SetWeatherState(.25f, 123.125);
      if (cy == -4 || cy == 19)
        frame.SetBlockLocal({1, 2, 3}, 8);
      const auto decoded = codec.Deserialize(
          codec.Serialize(coord, frame, registry).bytes, coord, registry);
      Expect(ApplyTerrainSliceLoad(source, coord, &decoded, nullptr, {column, 1},
                                  1, false), "full source frame decodes");
      if (cy == -4)
        Expect(ApplyTerrainSliceLoad(partial, coord, &decoded, nullptr,
                                    {column, 1}, 1, false),
               "partial save owns only the lower real decoded frame");
    }
    Expect(storage.SaveTerrainColumn(column, source, folder.string(), registry, 319, -64),
           "complete source reports successful persistence");
    // Keep a semantically valid but byte-distinct existing receipt. Even
    // rewriting unchanged metadata would violate refusal-before-first-write.
    {
      std::ofstream light(UChunkStorageService::ColumnLightFilePath(
                              folder.string(), column), std::ios::app);
      light << "\n";
      Expect(light.good(), "metadata refusal fixture has distinct original bytes");
    }
    const auto before = capture(folder);
    Expect(before.size() >= 24, "partial save fixture has actual indexed files");
    partial.SetBlock(deleted, BLOCK_AIR);
    partial.SetBlock(added, 8);
    partial.GetChunkManager().GetChunk(floor)->SetWeatherState(.75f, 456.125);
    const auto incarnation = partial.GetChunkManager().GetChunk(floor)->GetIncarnation();
    Expect(!partial.GetChunkManager().HasUnresolvedTerrainColumnRead(column),
           "unknown save must be rejected independently of failed-read marker");
    Expect(!storage.SaveTerrainColumn(column, partial, folder.string(), registry, 319, -64),
           "unmarked unknown range reports an explicit save refusal");
    Expect(capture(folder) == before && partial.GetBlock(deleted) == BLOCK_AIR &&
               partial.GetBlock(added) == 8 &&
               partial.GetChunkManager().GetChunk(floor)->GetIncarnation() == incarnation,
           "unmarked partial save must preserve every disk byte and RAM edit");
    const auto fresh = folder / "new-world";
    UBlockWorld new_partial;
    new_partial.SetBlock(added, 8);
    new_partial.SetBlock(glm::ivec3(-11, 19, 7) * CHUNK_SIZE, 8);
    Expect(!storage.SaveTerrainColumn(column, new_partial, fresh.string(), registry, 319, -64),
           "unknown new range reports an explicit save refusal");
    Expect(!std::filesystem::exists(fresh),
           "unknown range must not create a new incomplete terrain snapshot");
    Expect(storage.LoadTerrainColumn(column, partial, folder.string(), registry,
                                    319, -64) > 0 &&
               partial.GetBlock(deleted) == BLOCK_AIR && partial.GetBlock(added) == 8,
           "real reload completes the base without overwriting local edits");
    Expect(storage.SaveTerrainColumn(column, partial, folder.string(), registry, 319, -64),
           "recovered complete range reports successful persistence");
    UBlockWorld cold;
    Expect(storage.LoadTerrainColumn(column, cold, folder.string(), registry,
                                    319, -64) > 0 &&
               cold.GetBlock(deleted) == BLOCK_AIR && cold.GetBlock(added) == 8 &&
               cold.GetBlock(glm::ivec3(-11, 19, 7) * CHUNK_SIZE + glm::ivec3(1, 2, 3)) == 8 &&
               cold.GetChunkManager().GetChunk(floor)->GetWeatherLastUpdateSec() == 456.125,
           "completed save preserves edits, upper geometry and weather on cold reload");
    const auto blocked = folder / "not-a-directory";
    {
      std::ofstream file(blocked);
      file << "preserve this ordinary file";
    }
    const auto failure_before = capture(folder);
    Expect(!storage.SaveTerrainColumn(column, source, blocked.string(), registry,
                                     319, -64) &&
               capture(folder) == failure_before,
           "actual metadata write failure reports false without replacing its blocker");
    const auto resolved = std::filesystem::weakly_canonical(folder);
    Expect(resolved.parent_path() == std::filesystem::weakly_canonical(
               std::filesystem::temp_directory_path()),
           "partial save cleanup remains inside its unique system-temp fixture");
    std::filesystem::remove_all(resolved);
  }
}

static void TestColumnSaveReceipts()
{
  using namespace cutum;
  UBlockWorld world;
  const glm::ivec3 column(4, 0, -2), coord(4, -4, -2);
  const auto cell = coord * CHUNK_SIZE;
  world.SetBlock(cell, 8);
  auto &manager = world.GetChunkManager();
  TerrainColumnSaveStamp stamp(manager, column, -64, -17);
  Expect(stamp.Matches(manager), "new save stamp matches its actual terrain owner");
  world.SetBlock({900, 0, 900}, 8);
  Expect(stamp.Matches(manager), "unrelated columns cannot starve save completion");
  world.SetBlock(cell, BLOCK_AIR);
  Expect(!stamp.Matches(manager), "save ACK cannot retire a newer local deletion");
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  manager.GetChunk(coord)->SetWeatherState(.75f, 456.125);
  Expect(!stamp.Matches(manager), "weather mutation requires a new saved snapshot");
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  manager.GetChunk(coord)->SetLightLocal({0, 0, 0}, 5, 7);
  Expect(!stamp.Matches(manager), "light bytes cannot change under a save receipt");
  world.SetBlock(cell, 8);
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  manager.GetChunk(coord)->SetFluidLocal({0, 0, 0},
      FluidCellState::Flowing(3).WithKind(FluidKind::Water));
  Expect(!stamp.Matches(manager), "fluid mutation requires a fresh save receipt");
  UBlockWorld foreign;
  Expect(!stamp.Matches(foreign.GetChunkManager()), "foreign manager cannot consume save authority");
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  manager.RemoveChunk(coord);
  world.SetBlock(cell, 8);
  Expect(!stamp.Matches(manager), "same coordinate replacement is a new save obligation");

  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UChunkStorageService storage;
  UBinaryChunkSerializer codec;
  for (int cy = -3; cy <= -2; ++cy)
  {
    const glm::ivec3 slice(column.x, cy, column.z);
    UChunk empty(slice);
    const auto decoded = codec.Deserialize(codec.Serialize(slice, empty, registry).bytes,
                                            slice, registry);
    Expect(ApplyTerrainSliceLoad(world, slice, &decoded, nullptr, {column, 1}, 1, false),
           "save batch has actual geometry-free decoded AIR");
  }
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  world.SetBlock(glm::ivec3(column.x, -3, column.z) * CHUNK_SIZE, 8);
  Expect(!stamp.Matches(manager), "AIR materialization invalidates the old save stamp");
  const auto folder = std::filesystem::temp_directory_path() /
      ("cubatarium_save_receipts_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  Expect(!std::filesystem::exists(folder), "save receipt fixture must be fresh");
  UAsyncChunkIO io;
  constexpr uint64_t batch = 101;
  ColumnSaveCompletion completion(batch, folder.string(), column, -4, -2, true);
  io.RequestSaveColumnLight(column, std::make_shared<const PersistedColumnLight>(),
                            storage, folder.string(), batch);
  for (int cy = -4; cy <= -2; ++cy)
    io.RequestSave({column.x, cy, column.z}, storage, folder.string(), world,
                    registry, {column, 1}, batch);
  io.RequestRemoveChunkSlices(column, -1, 0, storage, folder.string(), batch);
  Expect(!completion.Complete() && io.WaitIdleFor(std::chrono::seconds(5)),
         "enqueue alone grants no completed-save authority");
  auto done = io.DrainSaves();
  Expect(done.size() == 5, "save batch includes terrain, metadata and cleanup ACKs");
  for (const auto &result : done)
    Expect(result.saveBatchId == batch && result.success,
           "actual worker preserves batch identity across every operation");
  auto bad = done.front();
  ++bad.saveBatchId;
  Expect(!completion.Consume(bad), "foreign batch cannot advance this save");
  bad = done.front(); bad.worldFolder += "foreign";
  Expect(!completion.Consume(bad), "foreign folder cannot advance this save");
  bad = done.front(); ++bad.groundCoord.x;
  Expect(!completion.Consume(bad), "foreign column cannot advance this save");
  bad = done.front(); bad.cleanupOnly = false; bad.coord.y = ColumnSaveCompletion::Light;
  Expect(!completion.Consume(bad), "terrain ACK cannot impersonate metadata completion");
  std::reverse(done.begin(), done.end());
  for (const auto &result : done)
  {
    Expect(completion.Consume(result), "each real expected operation is accepted once");
    Expect(!completion.Consume(result), "duplicate ACK cannot decrement a newer obligation");
  }
  Expect(completion.Succeeded(), "all actual successful writes complete their exact batch");
  stamp = TerrainColumnSaveStamp(manager, column, -64, -17);
  std::unordered_set<glm::ivec3, IVec3Hash> dirty{
      coord, {column.x, -3, column.z}, {column.x, -5, column.z}, {5, -4, -2}};
  Expect(AcknowledgeTerrainColumnEdits(manager, dirty, {5, 0, -2}, stamp, -64,
                                       -17) == 0 &&
             AcknowledgeTerrainColumnEdits(manager, dirty, column, stamp, -80,
                                           -17) == 0 &&
             dirty.size() == 4,
         "save stamp cannot acknowledge a foreign column or profile");
  Expect(AcknowledgeTerrainColumnEdits(manager, dirty, column, stamp, -64,
                                       -17) == 2 &&
             dirty.count({column.x, -5, column.z}) && dirty.count({5, -4, -2}),
         "successful current save acknowledges only its written column and "
         "profile");
  dirty.insert(coord);
  world.SetBlock(cell, BLOCK_AIR);
  Expect(AcknowledgeTerrainColumnEdits(manager, dirty, column, stamp, -64,
                                       -17) == 0 &&
             dirty.count(coord),
         "post-write deletion stays dirty after stale completion");
  ColumnSaveCompletion newer(batch + 1, folder.string(), column, -4, -2, true);
  for (const auto &result : done)
    Expect(!newer.Consume(result), "late old results cannot finish a newer save");
  Expect(!newer.Complete(), "late old ACKs grant no new retirement authority");
  const auto blocked = folder / "file-blocker";
  std::ofstream(blocked) << "not a directory";
  io.RequestSaveColumnLight(column, std::make_shared<const PersistedColumnLight>(),
                            storage, blocked.string(), batch + 2);
  Expect(io.WaitIdleFor(std::chrono::seconds(5)), "failed real metadata write is bounded");
  auto failed = io.DrainSaves();
  ColumnSaveCompletion failure(batch + 2, blocked.string(), column, 0, -1, false);
  Expect(failed.size() == 1 && !failed[0].success && failure.Consume(failed[0]) &&
             failure.Complete() && !failure.Succeeded(),
         "completed failed batch must never authorize terrain retirement");
  const auto resolved = std::filesystem::weakly_canonical(folder);
  Expect(resolved.parent_path() == std::filesystem::weakly_canonical(
             std::filesystem::temp_directory_path()), "save receipt cleanup is confined");
  std::filesystem::remove_all(resolved);
}

static void TestLoadedAirSlice()
{
  using namespace cutum;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBinaryChunkSerializer binary;
  UJsonChunkSerializer json;
  const glm::ivec3 coord(2, -3, -3), column(2, 0, -3);
  const auto cell = coord * CHUNK_SIZE + glm::ivec3(1, 2, 3);
  UChunk carrier(coord);
  std::array<uint8_t, CHUNK_VOLUME> pattern{};
  for (size_t i = 0; i < pattern.size(); ++i)
    pattern[i] = static_cast<uint8_t>((i * 37 + 113) & 255);
  carrier.ReplaceLightData(pattern);
  carrier.SetWeatherState(0.25f, 123.456);
  const auto encoded = binary.Serialize(coord, carrier, registry);
  auto decoded = binary.Deserialize(encoded.bytes, coord, registry);
  auto air = decoded.GetDecodedAirSlice(coord);
  Expect(air.has_value(), "complete CCHK4 certifies decoded AIR geometry");
  Expect(!decoded.GetDecodedAirSlice(coord + glm::ivec3(0, 1, 0)),
         "decoded AIR cannot be rebound to a foreign slice");
  UChunkBuffer untrusted;
  untrusted.SetChunkLightData(coord, pattern);
  untrusted.SetChunkWeatherState(coord, 0.25f, 123.456);
  Expect(!untrusted.GetDecodedAirSlice(coord),
         "raw metadata does not mint decoded AIR authority");
  auto modified = decoded;
  modified.SetBlock(cell, 8);
  modified.SetBlock(cell, BLOCK_AIR);
  Expect(!modified.GetDecodedAirSlice(coord),
         "even an edit erased back to AIR invalidates decode authority");
  modified = decoded;
  modified.SetFluidPacked(cell, 1);
  Expect(!modified.GetDecodedAirSlice(coord), "fluid payload is not empty AIR");
  modified = decoded;
  modified.SetLightPacked(cell, 0);
  Expect(!modified.GetDecodedAirSlice(coord),
         "post-decode light write invalidates seal");
  modified = decoded;
  modified.Clear();
  Expect(!modified.GetDecodedAirSlice(coord), "clear removes decode authority");
  for (const size_t size :
       {size_t(0), size_t(19), size_t(28), encoded.bytes.size() - 1})
  {
    const std::vector<uint8_t> truncated(encoded.bytes.begin(),
                                         encoded.bytes.begin() + size);
    Expect(!binary.Deserialize(truncated, coord, registry)
                .GetDecodedAirSlice(coord),
           "truncated binary never proves AIR");
  }
  auto corrupt = encoded.bytes;
  corrupt.push_back(0);
  Expect(
      !binary.Deserialize(corrupt, coord, registry).GetDecodedAirSlice(coord),
      "unconsumed binary bytes deny AIR authority");
  corrupt = encoded.bytes;
  corrupt[29] = 0;
  corrupt[30] = 0;
  corrupt[31] = 0;
  corrupt[32] = 0;
  Expect(
      !binary.Deserialize(corrupt, coord, registry).GetDecodedAirSlice(coord),
      "unrecognized fluid length cannot prove AIR");
  Expect(
      !binary.Deserialize(encoded.bytes, coord + glm::ivec3(0, 1, 0), registry)
           .GetDecodedAirSlice(coord + glm::ivec3(0, 1, 0)),
      "binary header mismatch cannot prove foreign AIR");
  for (int version = 1; version <= 3; ++version)
  {
    auto legacy = encoded.bytes;
    legacy[4] = static_cast<uint8_t>(version);
    legacy.resize(29 + (version >= 2 ? 4100 : 0) + (version >= 3 ? 4100 : 0));
    Expect(binary.Deserialize(legacy, coord, registry)
               .GetDecodedAirSlice(coord)
               .has_value(),
           "complete CCHK1..3 AIR retains backward compatibility");
  }
  auto nonair = carrier;
  nonair.SetBlockLocal({1, 2, 3}, 8);
  const auto solid = binary.Serialize(coord, nonair, registry);
  Expect(!binary.Deserialize(solid.bytes, coord, registry)
              .GetDecodedAirSlice(coord),
         "decoded solid slice cannot mint AIR");
  const auto json_bytes = json.Serialize(coord, carrier, registry);
  Expect(json.Deserialize(json_bytes.bytes, coord, registry)
             .GetDecodedAirSlice(coord)
             .has_value(),
         "JSON4 empty geometry has explicit decoded authority");
  const auto decode_json = [&](const std::string &text)
  {
    return json.Deserialize(std::vector<uint8_t>(text.begin(), text.end()),
                            coord, registry);
  };
  Expect(decode_json(R"({"voxels":[]})").GetDecodedAirSlice(coord).has_value(),
         "legacy sparse JSON without coordinates is accepted");
  for (const char *bad :
       {R"({"voxels":{}})", R"({"voxels":[],"cx":2})",
        R"({"voxels":[],"format_version":99})",
        R"({"voxels":[{"lx":0,"ly":0,"lz":0,"type":"missing_block"}]})",
        R"({"voxels":[{"lx":16,"ly":0,"lz":0,"id":0}]})",
        R"({"voxels":[{"lx":0.5,"ly":0,"lz":0,"id":0}]})",
        R"({"voxels":[{"lx":0,"ly":0,"lz":0,"id":65536}]})",
        R"({"voxels":[{"lx":0,"ly":0,"lz":0,"id":0,"fluid":1}]})",
        R"({"voxels":[],"weather_state":false})"})
    Expect(!decode_json(bad).GetDecodedAirSlice(coord),
           "malformed or ambiguous sparse JSON never proves AIR");

  {
    UBlockWorld lower_only;
    const glm::ivec3 lower(coord.x, coord.y-1, coord.z);
    lower_only.GetChunkManager().EnsureChunk(lower);
    lower_only.GetChunkManager().GetChunk(lower)->SetBlockLocal({1, 1, 1}, 8);
    const auto unknown = lower_only.CaptureInputStamp(coord, false, true);
    Expect(lower_only.QueryBlock(cell).IsUnloaded() &&
               !lower_only.IsKnownAbsentAirChunk(coord) &&
               unknown.missingEvidence == MissingChunkEvidence::Unknown,
           "one lower resident section cannot prove an unloaded upper section is AIR");
    bool complete = false, pending = false;
    lower_only.SetKnownAirColumnFloorQuery(
        [&](glm::ivec3) -> std::optional<int>
        { return complete && !pending ? std::optional<int>(coord.y) : std::nullopt; });
    Expect(!lower_only.IsKnownAbsentAirChunk(coord),
           "an incomplete declared producer retains unknown geometry");
    complete = true;
    Expect(lower_only.IsKnownAbsentAirChunk(coord) &&
               !lower_only.InputStampStillMatches(unknown),
           "producer completion changes absence knowledge without a voxel write");
    const auto known = lower_only.CaptureInputStamp(coord, false, true);
    pending = true;
    Expect(!lower_only.IsKnownAbsentAirChunk(coord) &&
               !lower_only.InputStampStillMatches(known),
           "pending persistence immediately revokes the retained AIR stamp");
    pending = false;
    Expect(lower_only.InputStampStillMatches(known),
           "current completed producer evidence restores the same absence fact");
  }
  UBlockWorld world;
  auto &manager = world.GetChunkManager();
  manager.EnsureSectionLightField(coord);
  Expect(manager.QueryBlock(cell).IsUnloaded() &&
             !world.IsKnownAbsentAirChunk(coord),
         "field allocation alone retains unknown geometry");
  const auto prior_field = manager.GetSectionLightField(coord);
  const auto revision = manager.GetGeometryInputRevision();
  Expect(!manager.InstallLoadedAirSlice(*air, {column, 7}, 8, false) &&
             !manager.InstallLoadedAirSlice(*air, {column, 8}, 8, true) &&
             !manager.InstallLoadedAirSlice(*air, {coord, 8}, 8, false) &&
             manager.GetGeometryInputRevision() == revision,
         "stale, cancelled and wrong-column commits are side-effect free");
  Expect(manager.InstallLoadedAirSlice(*air, {column, 8}, 8, false),
         "current owned completion installs known AIR");
  const auto field = manager.GetSectionLightField(coord);
  Expect(field != prior_field && field->GetData() == pattern &&
             field->GetPaddedDomains() == 0 && !field->GetPaddedData(),
         "legacy bytes get fresh field ownership without computed authority");
  Expect(manager.GetResidentChunkCount() == 0 &&
             manager.GetResidentColumnCount() == 0 &&
             manager.QueryBlock(cell).IsAir() &&
             world.IsKnownAbsentAirChunk(coord),
         "loaded AIR has explicit knowledge without geometry holders");
  Expect(world.GetLightCalculationState(
             coord, LightDomain::Block,
             registry.GetDefinitionsCatalogSnapshot()) ==
             LightCalculationState::Missing,
         "loaded light bytes cannot masquerade as completed light");
  Expect(!manager.ReleaseAbsentSectionLightField(coord) &&
             manager.RetireAbsentSectionLightFields(100, [](glm::ivec3)
                                                    { return false; }) == 0,
         "loaded AIR owns legacy light until terrain retirement");
  const auto clock = manager.GetInputRevision();
  const auto geometry_clock = manager.GetGeometryInputRevision();
  auto saved = manager.MakeTerrainSaveSnapshot(coord);
  Expect(saved && saved->IsAirOnly() && saved->GetLightData() == pattern &&
             std::abs(saved->GetSurfaceWetness() - 0.25f) < 0.00002f &&
             std::abs(saved->GetWeatherLastUpdateSec() - 123.456) < 0.001 &&
             manager.GetInputRevision() == clock &&
             manager.GetGeometryInputRevision() == geometry_clock &&
             manager.GetResidentChunkCount() == 0 &&
             !manager.MakeTerrainSaveSnapshot(coord + glm::ivec3(0, 1, 0)),
         "save carrier preserves metadata without changing live clocks or "
         "inventing unknown slices");
  Expect(binary.Serialize(coord, *saved, registry).bytes == encoded.bytes,
         "detached save carrier reproduces exact AIR CCHK4 frame");
  Expect(!manager.InstallLoadedAirSlice(*air, {column, 8}, 8, false),
         "duplicate completion cannot reset field ownership");
  manager.SetBlock(cell, 8);
  const auto *materialized = manager.GetChunk(coord);
  Expect(materialized && !manager.HasLoadedAirSlice(coord) &&
             materialized->GetLightData() == pattern &&
             std::abs(materialized->GetSurfaceWetness() - 0.25f) < 0.00002f &&
             manager.GetSectionLightField(coord) == field,
         "real edit materializes geometry while retaining canonical light and "
         "weather");
  Expect(!manager.InstallLoadedAirSlice(*air, {column, 8}, 8, false) &&
             manager.GetBlock(cell) == 8,
         "late AIR completion cannot discard resident edits");
  manager.RemoveChunk(coord);
  Expect(manager.QueryBlock(cell).IsUnloaded() &&
             !manager.GetSectionLightField(coord),
         "retiring materialized slice clears AIR and light ownership");
  Expect(manager.InstallLoadedAirSlice(*air, {column, 9}, 9, false) &&
             manager.GetSectionLightField(coord)->GetIncarnation() !=
                 field->GetIncarnation(),
         "reload starts a fresh field incarnation");
  manager.RemoveChunk(coord);
  Expect(!manager.HasLoadedAirSlice(coord) &&
             !manager.GetSectionLightField(coord) &&
             manager.QueryBlock(cell).IsUnloaded() &&
             !world.IsKnownAbsentAirChunk(coord),
         "retiring geometry-free AIR clears both knowledge and bytes");
  Expect(manager.InstallLoadedAirSlice(*air, {column, 0}, 0, false),
         "initial generation zero is a valid owner");
  manager.Clear();
  Expect(!manager.HasLoadedAirSlice(coord) &&
             manager.GetSectionLightFieldCount() == 0,
         "manager clear removes loaded AIR ownership");
}

int main()
{
  {
    cutum::DiskReadFailureProbe disabled;
    Expect(!disabled.Enabled() && !disabled.InjectFor({1, 19, 5}) &&
               !disabled.CheckSaveOnce(),
           "read probe defaults off");
    cutum::DiskReadFailureProbe probe(glm::ivec2(1, 5));
    probe.ObserveFocus({1, 0, 3});
    Expect(!probe.InjectFor({1, 19, 5}), "startup cannot inject read failures");
    probe.ObserveFocus({-15, 0, 3});
    Expect(!probe.InjectFor({2, 19, 5}) && !probe.InjectFor({1, 18, 5}),
           "wrong frame cannot consume read probe budget");
    for (int n = 0; n < 5; ++n)
      Expect(probe.InjectFor({1, 19, 5}),
             "five real returning requests may inject");
    Expect(!probe.InjectFor({1, 19, 5}) && probe.CheckSaveOnce() &&
               !probe.CheckSaveOnce(),
           "injection budget closes and save check is one-shot");
  }
  {
    using cutum::DiskAirCancellationProbe;
    Expect(!DiskAirCancellationProbe{}.Enabled(),
           "native cancellation probe defaults off");
    for (const char *bad : {"", "1", "1,", ",5", "1,5x", "1,5,6",
                            "2147483648,0", "0,-2147483649"})
      Expect(!DiskAirCancellationProbe::Parse(bad),
             "probe rejects invalid coordinates");
    Expect(DiskAirCancellationProbe::Parse("-2147483648,2147483647") ==
               glm::ivec2(INT_MIN, INT_MAX),
           "probe accepts signed coordinates without overflow");
    DiskAirCancellationProbe probe(glm::ivec2(1, 5));
    probe.ObserveFocus({1, 0, 3});
    Expect(!probe.TryCancel({1, 0, 5}, true, false, 23, true),
           "startup cannot trigger returning-demand cancellation");
    probe.ObserveFocus({-15, 0, 3});
    Expect(
        !probe.TryCancel({2, 0, 5}, true, false, 23, true) &&
            !probe.TryCancel({1, 0, 5}, false, false, 23, true) &&
            !probe.TryCancel({1, 0, 5}, true, true, 23, true) &&
            !probe.TryCancel({1, 0, 5}, true, false, 0, true) &&
            !probe.TryCancel({1, 0, 5}, true, false, 23, false),
        "wrong column, duplicate result, geometry, complete load and unstaged "
        "result cannot fire probe");
    Expect(probe.TryCancel({1, 0, 5}, true, false, 23, true) &&
               !probe.TryCancel({1, 0, 5}, true, false, 23, true),
           "probe fires exactly once after real excursion and a partial owned "
           "AIR install");
  }
  TestLoadedAirSlice();
  TestDiskRetryReadSafety();
  TestDiskRetryStorage();
  TestColumnSaveReceipts();
  TestUnmarkedPartialSavePreservesDisk();
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
  TestAirTerrainIo(world_folder.string() + "_air_io");
  TestAirOnlyColumnStorage(world_folder.string() + "_air_only");

  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UChunkStorageService storage;
  const auto atomicTarget = world_folder / "atomic-write-probe.bin";
  const std::vector<uint8_t> firstBytes{1, 2, 3};
  const std::vector<uint8_t> replacementBytes{4, 5, 6, 7};
  Expect(storage.WriteBytesAtomically(atomicTarget.string(), firstBytes),
         "initial atomic file write succeeds");
  Expect(storage.WriteBytesAtomically(atomicTarget.string(), replacementBytes),
         "atomic write replaces an existing committed file");
  // A directory at the temporary path deterministically rejects opening it.
  const auto blockedTemp = std::filesystem::path(atomicTarget.string() + ".tmp");
  std::filesystem::create_directory(blockedTemp);
  std::error_code atomicFailure;
  Expect(!storage.WriteBytesAtomically(atomicTarget.string(), firstBytes,
                                       &atomicFailure),
         "temporary-file open failure is reported");
  Expect(bool(atomicFailure), "failed atomic write retains its error code");
  std::ifstream committed(atomicTarget, std::ios::binary);
  const std::vector<uint8_t> retained((std::istreambuf_iterator<char>(committed)),
                                      std::istreambuf_iterator<char>());
  committed.close();
  Expect(retained == replacementBytes,
         "failed write retains the previous committed bytes");
  std::filesystem::remove(blockedTemp);
  Expect(storage.WriteBytesAtomically(atomicTarget.string(), firstBytes,
                                      &atomicFailure) && !atomicFailure,
         "successful retry clears the previous write error");
#ifdef _WIN32
  Expect(std::error_code(ERROR_DISK_FULL, std::system_category()) ==
             std::errc::no_space_on_device,
         "Windows disk-full error classifies as no space");
  Expect(std::error_code(ERROR_HANDLE_DISK_FULL, std::system_category()) ==
             std::errc::no_space_on_device,
         "Windows handle disk-full error classifies as no space");
  const HANDLE lockedTarget = CreateFileW(
      atomicTarget.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Expect(lockedTarget != INVALID_HANDLE_VALUE,
         "replacement failure fixture locks the committed file");
  const bool lockedWrite = storage.WriteBytesAtomically(
      atomicTarget.string(), replacementBytes, &atomicFailure);
  CloseHandle(lockedTarget);
  Expect(!lockedWrite && bool(atomicFailure),
         "locked target replacement reports a filesystem error");
  std::ifstream lockedResult(atomicTarget, std::ios::binary);
  const std::vector<uint8_t> lockedBytes(
      (std::istreambuf_iterator<char>(lockedResult)),
      std::istreambuf_iterator<char>());
  lockedResult.close();
  Expect(lockedBytes == firstBytes,
         "failed replacement preserves the last committed target");
  Expect(!std::filesystem::exists(blockedTemp),
         "failed replacement cleans its unpublished temporary file");
#endif
  Expect(storage.WriteStorageMarker(world_folder.string(), &atomicFailure) &&
             !atomicFailure,
         "storage marker commits successfully");
  const auto markerPath = world_folder / "chunks.json";
  std::ifstream originalMarker(markerPath, std::ios::binary);
  const std::string markerBefore((std::istreambuf_iterator<char>(originalMarker)),
                                 std::istreambuf_iterator<char>());
  originalMarker.close();
  const auto blockedMarker =
      std::filesystem::path(markerPath.string() + ".tmp");
  std::filesystem::create_directory(blockedMarker);
  Expect(!storage.WriteStorageMarker(world_folder.string(), &atomicFailure) &&
             bool(atomicFailure),
         "storage marker reports temporary-file failure");
  std::ifstream retainedMarker(markerPath, std::ios::binary);
  const std::string markerAfter((std::istreambuf_iterator<char>(retainedMarker)),
                                std::istreambuf_iterator<char>());
  retainedMarker.close();
  Expect(!markerBefore.empty() && markerAfter == markerBefore,
         "failed storage marker write preserves previous format metadata");
  std::filesystem::remove(blockedMarker);
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

  // The fixture intentionally has empty internal sections. Declare them
  // through real decoded frames; saving missing allocations is not AIR proof.
  UBinaryChunkSerializer source_codec;
  for (int cy = -3; cy <= 18; ++cy)
  {
    const glm::ivec3 coord(ground.x, cy, ground.z);
    UChunk empty(coord);
    const auto decoded = source_codec.Deserialize(
        source_codec.Serialize(coord, empty, registry).bytes, coord, registry);
    Expect(ApplyTerrainSliceLoad(source, coord, &decoded, nullptr,
                                {ground, 1}, 1, false),
           "expanded fixture explicitly decodes its empty internal sections");
  }
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
