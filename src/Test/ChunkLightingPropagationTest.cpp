#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Diagnostics/JobStageTrace.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/GpuFullLightingPipeline.h"
#include "World/Lighting/LightUtil.h"
#include "World/Lighting/PersistedSectionLight.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace cutum
{
void SetTestSkylightSeedAvailable(bool available);
uint64_t UJobStageTrace::GetStreamingFrameEpoch() { return 0; }
void CubatariumLogInfo(const char *, const std::string &) {}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_lighting_propagation_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockRegistry> MakeRegistry()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = kStone;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition torch;
  torch.Name = "torch";
  torch.Id = kTorch;
  torch.Physics = cutum::BlockPhysicsProfile::Solid();
  torch.Render.Style = cutum::BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Id = 9;
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kTorch] = torch;
  by_id[9] = water;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["torch"] = kTorch;
  name_to_id["water"] = 9;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return std::make_shared<cutum::UBlockRegistry>(nullptr, definitions);
}

static int SkyAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetSkyLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static int BlockAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetBlockLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static void BuildEnclosedRoom(cutum::UBlockWorld &world, cutum::BlockId stone,
                              int floor_y)
{
  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, floor_y, z), stone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), stone);
      world.SetBlock(glm::ivec3(x, y, 11), stone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), stone);
      world.SetBlock(glm::ivec3(11, y, z), stone);
    }
  }
}

static void TestSyncContract(const std::string &mode)
{
  using namespace cutum;
  auto registry = MakeRegistry();
  UBlockWorld world;
  world.GetChunkManager().EnsureChunk({0, -3, 0});
  auto *target = world.GetChunkManager().GetChunk({0, -3, 0});
  if (mode == "--unknown-boundary")
  {
    std::vector<glm::ivec3> changed;
    ApplyEnterOpenSkyBoundary(world, *registry, 0, 0, -48, -33, &changed);
    Expect(target->GetSkyLightLocal({15, 8, 8}) == 0,
           "unknown unloaded border cannot inject maximum daytime sky");
    return;
  }
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  if (mode == "--sparse-sky")
  {
    // Read-only proven-air sections between two writable sections still carry
    // incoming direct sky. A noncontiguous write set must not create a roof.
    world.GetChunkManager().EnsureChunk({0, 19, 0});
    RelightJobSpec spec;
    spec.min_world_y = -64;
    spec.max_world_y = 319;
    const auto result = UChunkRelightSnapshot::CaptureSections(
                            world, spec, {{0, -3, 0}, {0, 19, 0}})
                            .Compute(*registry);
    const auto it = std::find_if(result.chunks.begin(), result.chunks.end(),
                                 [](const auto &c)
                                 { return c.coord == glm::ivec3(0, -3, 0); });
    Expect(it != result.chunks.end() &&
               UnpackSky(it->light_packed[UChunk::LocalIndex({8, 8, 8})]) == 15,
           "known empty vertical gaps carry direct sky between sparse write "
           "sections");
    return;
  }
  if (mode == "--sync-parity")
  {
    world.SetBlock({16, -40, 8}, 50);
    world.SetBlock({6, -39, 8}, 8);
    auto *neighbor = world.GetChunkManager().GetChunk({1, -3, 0});
    neighbor->SetLightLocal({0, 8, 8}, 3, 15);
    const auto halo = neighbor->GetLightData();
    for (int mask : {1, 2, 3})
    {
      std::array<uint8_t, CHUNK_VOLUME> poison;
      poison.fill(PackLight(2, 7));
      target->ReplaceLightData(poison);
      RelightJobSpec spec;
      spec.min_world_y = -64;
      spec.max_world_y = 319;
      spec.include_skylight = (mask & 1) != 0;
      spec.include_block_light = (mask & 2) != 0;
      auto result =
          UChunkRelightSnapshot::CaptureSections(world, spec, {{0, -3, 0}})
              .Compute(*registry);
      Expect(result.chunks.size() == 1 && result.inputs_complete,
             "explicit capture writes one complete section");
      RelightChunk(world, *registry, {0, -3, 0}, spec.include_block_light,
                   spec.include_skylight);
      for (int i = 0; i < CHUNK_VOLUME; ++i)
      {
        const auto expected = PackLight(
            spec.include_skylight ? UnpackSky(result.chunks[0].light_packed[i])
                                  : 2,
            spec.include_block_light
                ? UnpackBlock(result.chunks[0].light_packed[i])
                : 7);
        Expect(target->GetLightData()[i] == expected,
               "sync and async solver agree for every cell and preserve "
               "uncomputed channel");
      }
      Expect(halo == neighbor->GetLightData(),
             "domain solve preserves neighboring live fields");
    }
    return;
  }
  if (mode == "--edit-contract")
  {
    world.SetBlock({15, -40, 8}, 50);
    world.GetChunkManager().EnsureChunk({1, -3, 0});
    RelightBlocksAroundAll(world, *registry, {{15, -40, 8}},
                           world.GetVerticalProfile().MaxY);
    Expect(BlockAt(world, {15, -40, 8}) == 14,
           "full relight includes sections below zero");
    RelightBlocksAroundEdit(world, *registry, {{15, -40, 8}});
    Expect(BlockAt(world, {16, -40, 8}) == 13,
           "edit lights neighboring section across negative-Y seam");
    world.SetBlock({15, -40, 8}, BLOCK_AIR);
    RelightBlocksAroundEdit(world, *registry, {{15, -40, 8}});
    Expect(BlockAt(world, {16, -40, 8}) == 0,
           "source removal clears old neighboring emission");
    for (int x = -16; x < 32; ++x)
      for (int z = -16; z < 32; ++z)
        world.SetBlock({x, 304, z}, 8);
    RelightBlocksAroundEdit(world, *registry, {{8, 304, 8}});
    Expect(SkyAt(world, {8, -40, 8}) == 0,
           "high roof edit darkens deep loaded section");
    world.SetBlock({8, 304, 8}, BLOCK_AIR);
    RelightBlocksAroundEdit(world, *registry, {{8, 304, 8}});
    Expect(SkyAt(world, {8, -40, 8}) == 15,
           "opening high shaft relights deep section across proven empty gaps");
    const auto revision = target->GetLightFieldRevision();
    RelightBlocksAroundEdit(world, *registry, {{8, 304, 8}});
    Expect(revision == target->GetLightFieldRevision(),
           "no-op edit calculation keeps stable light identity");
    return;
  }
  if (mode == "--sync-noop")
  {
    world.SetBlock({8, -40, 8}, 50);
    RelightChunk(world, *registry, {0, -3, 0});
    const auto before = target->GetLightData();
    const auto revision = target->GetLightFieldRevision();
    RelightChunk(world, *registry, {0, -3, 0});
    Expect(before == target->GetLightData(),
           "unchanged sync inputs produce identical light bytes");
    Expect(revision == target->GetLightFieldRevision(),
           "no-op sync calculation preserves light field identity");
    return;
  }
  world.SetBlock({16, -40, 8}, 50);
  auto *neighbor = world.GetChunkManager().GetChunk({1, -3, 0});
  if (mode == "--sync-neighbor")
    neighbor->SetLightLocal({0, 8, 8}, 0, 14);
  const auto halo_before = neighbor->GetLightData();
  const auto halo_revision = neighbor->GetLightFieldRevision();
  RelightChunk(world, *registry, {0, -3, 0}, true, false);
  if (mode == "--sync-neighbor")
    Expect(target->GetBlockLightLocal({15, 8, 8}) == 13,
           "sync calculation derives source geometry even with already lit "
           "neighboring field");
  else
    Expect(
        halo_before == neighbor->GetLightData() &&
            halo_revision == neighbor->GetLightFieldRevision(),
        "single-section sync calculation never mutates read-only halo light");
}

static void TestPersistedSectionLight()
{
  using namespace cutum;
  auto registry = MakeRegistry();
  const auto catalog = registry->GetDefinitionsCatalogSnapshot();
  const glm::ivec3 root(-2, -3, 1);
  static_assert(!std::is_default_constructible_v<RelightContentCertificate>);
  UBlockWorld unknown;
  unknown.GetChunkManager().EnsureChunk(root);
  RelightJobSpec unknown_spec;
  unknown_spec.min_world_y = -64;
  unknown_spec.max_world_y = 319;
  const auto transient =
      UChunkRelightSnapshot::CaptureSections(unknown, unknown_spec, {root})
          .Compute(*registry);
  Expect(!transient.inputs_complete && !transient.content_certificate,
         "unknown snapshot cannot mint frozen content evidence");
  const glm::ivec3 source = root * CHUNK_SIZE + glm::ivec3(16, 8, 8);
  const glm::ivec3 roof =
      root * CHUNK_SIZE + glm::ivec3(8, 0, 8) + glm::ivec3(0, 352, 0);
  const auto populate = [&](UBlockWorld &world, bool resident)
  {
    world.SetLightingConfigurationIdentity(1);
    world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    world.SetBlock(source, 50);
    world.SetBlock(roof, 8);
    if (resident)
      world.GetChunkManager().EnsureChunk(root);
  };
  for (const bool resident : {false, true})
  {
    UBlockWorld original;
    populate(original, resident);
    Expect(!CaptureSectionLight(original, root, catalog),
           "raw or missing light cannot produce a persisted receipt");
    RelightJobSpec spec;
    spec.min_world_y = -64;
    spec.max_world_y = 319;
    spec.include_known_air_outputs = true;
    const auto computed =
        UChunkRelightSnapshot::CaptureSections(original, spec, {root})
            .Compute(*registry);
    Expect(computed.inputs_complete && computed.chunks.size() == 1,
           "persisted fixture has a complete actual sky/block calculation");
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *computed.chunks[0].padded_light, true, true);
    Expect(original.RecordLightCalculation(computed, catalog) == 1,
           "persisted fixture records the installed calculation");
    const auto receipt = CaptureSectionLight(original, root, catalog);
    Expect(
        receipt &&
            UnpackBlock(
                receipt->volume.cells[SectionLightVolume::Index(15, 8, 8)]) ==
                13,
        "persisted receipt retains nonzero transport across the section "
        "border");
    const auto refreshed_catalog =
        std::make_shared<const BlockDefinitionCatalog>(*catalog);
    const auto after_refresh =
        CaptureSectionLight(original, root, refreshed_catalog);
    Expect(after_refresh &&
               after_refresh->volume.cells == receipt->volume.cells &&
               after_refresh->signature == receipt->signature &&
               !original.HasCompleteLightSamplingCalculation(root,
                                                             refreshed_catalog),
           "equivalent catalog refresh exports proven content without granting "
           "runtime pointer compatibility");
    auto changed_export_catalog =
        std::make_shared<BlockDefinitionCatalog>(*catalog);
    changed_export_catalog->ById.at(50).Lighting.Emission = 12;
    Expect(!CaptureSectionLight(original, root, changed_export_catalog),
           "changed emission cannot export an old catalog calculation");
    changed_export_catalog = std::make_shared<BlockDefinitionCatalog>(*catalog);
    changed_export_catalog->ById.at(8).Physics.Movement.Occupancy = 0;
    Expect(!CaptureSectionLight(original, root, changed_export_catalog),
           "changed transmission cannot export an old catalog calculation");
    const auto encoded = EncodeSectionLight(*receipt);
    const auto decoded =
        DecodeSectionLight(nlohmann::json::parse(encoded.dump()));
    Expect(decoded &&
               decoded->volume.cells == computed.chunks[0].padded_light->cells,
           "all 8000 padded cells round-trip without runtime identities");
    UBlockWorld reloaded;
    populate(reloaded, resident);
    const auto cloned_catalog =
        std::make_shared<const BlockDefinitionCatalog>(*catalog);
    Expect(cloned_catalog != catalog &&
               RestoreSectionLight(reloaded, *decoded, cloned_catalog) ==
                   SectionLightRestoreResult::Restored &&
               reloaded.HasCompleteLightSamplingCalculation(root,
                                                            cloned_catalog) &&
               reloaded.GetChunkManager()
                       .GetSectionLightField(root)
                       ->GetPaddedData()
                       ->cells == receipt->volume.cells &&
               bool(reloaded.GetChunkManager().GetChunk(root)) == resident,
           "reload restores calculated padded light with fresh "
           "catalog/field/geometry identities including AIR");
    Expect(reloaded.GetChunkManager()
                   .GetSectionLightField(root)
                   ->GetIncarnation() != original.GetChunkManager()
                                             .GetSectionLightField(root)
                                             ->GetIncarnation(),
           "disk receipts never preserve runtime field incarnation");
    reloaded.SetBlock(source, BLOCK_AIR);
    Expect(
        !reloaded.HasCompleteLightSamplingCalculation(root, cloned_catalog) &&
            RestoreSectionLight(reloaded, *decoded, cloned_catalog) ==
                SectionLightRestoreResult::Rejected,
        "changed neighbor content rejects saved light even with matching "
        "global signature");
    const auto historical = CaptureSectionLight(reloaded, root, cloned_catalog);
    Expect(
        historical && historical->checksum == receipt->checksum &&
            !reloaded.HasCompleteLightSamplingCalculation(root, cloned_catalog),
        "stale geometry exports only the original frozen artifact without "
        "runtime readiness");
    SectionLightRestoreDiagnostic diagnostic;
    UBlockWorld changed_roof;
    populate(changed_roof, resident);
    changed_roof.SetBlock(roof, BLOCK_AIR);
    Expect(RestoreSectionLight(changed_roof, *decoded, catalog, &diagnostic) ==
                   SectionLightRestoreResult::Rejected &&
               diagnostic.reason ==
                   SectionLightRestoreFailure::ContentMismatch &&
               diagnostic.has_input && diagnostic.current_known &&
               diagnostic.expected != diagnostic.current &&
               diagnostic.input_coord == UChunkManager::WorldToChunk(roof) &&
               (!changed_roof.GetChunkManager().GetSectionLightField(root) ||
                !changed_roof.GetChunkManager()
                     .GetSectionLightField(root)
                     ->GetPaddedData()),
           "far upper sky context is validated before installing any field");
    UBlockWorld unavailable;
    unavailable.SetLightingConfigurationIdentity(1);
    Expect(RestoreSectionLight(unavailable, *decoded, catalog, &diagnostic) ==
                   SectionLightRestoreResult::InputsUnavailable &&
               diagnostic.reason == SectionLightRestoreFailure::UnknownInput &&
               diagnostic.has_input && !diagnostic.current_known &&
               !unavailable.GetChunkManager().GetSectionLightField(root),
           "unloaded unknown dependencies defer restore without allocating a "
           "field");
    populate(unavailable, resident);
    Expect(
        RestoreSectionLight(unavailable, *decoded, catalog, &diagnostic) ==
                SectionLightRestoreResult::Restored &&
            diagnostic.reason == SectionLightRestoreFailure::None &&
            !diagnostic.has_input,
        "late known dependencies allow the original disk receipt to restore");
    UBlockWorld incompatible;
    populate(incompatible, resident);
    auto profile = incompatible.GetVerticalProfile();
    ++profile.SeaLevelY;
    incompatible.SetVerticalProfile(profile);
    Expect(RestoreSectionLight(incompatible, *decoded, catalog, &diagnostic) ==
                   SectionLightRestoreResult::Rejected &&
               diagnostic.reason == SectionLightRestoreFailure::Profile,
           "all vertical profile fields participate in disk compatibility");
    incompatible.SetVerticalProfile(original.GetVerticalProfile());
    incompatible.SetLightingConfigurationIdentity(0);
    Expect(RestoreSectionLight(incompatible, *decoded, catalog, &diagnostic) ==
                   SectionLightRestoreResult::Rejected &&
               diagnostic.reason == SectionLightRestoreFailure::Configuration,
           "lighting configuration changes reject persisted completion");
    incompatible.SetLightingConfigurationIdentity(1);
    auto changed_catalog = std::make_shared<BlockDefinitionCatalog>(*catalog);
    changed_catalog->ById.at(50).Lighting.Emission = 12;
    Expect(RestoreSectionLight(incompatible, *decoded, changed_catalog,
                               &diagnostic) ==
                   SectionLightRestoreResult::Rejected &&
               diagnostic.reason == SectionLightRestoreFailure::Materials,
           "optical material changes reject persisted completion");
    auto damaged = *decoded;
    damaged.volume.cells[0] ^= 1;
    Expect(RestoreSectionLight(incompatible, damaged, catalog, &diagnostic) ==
                   SectionLightRestoreResult::Rejected &&
               diagnostic.reason == SectionLightRestoreFailure::Checksum &&
               !DecodeSectionLight(EncodeSectionLight(damaged)),
           "corrupted padded bytes fail both disk and typed restore checksums");
    damaged = *decoded;
    damaged.inputs.erase(damaged.inputs.begin());
    damaged.checksum = persisted_light_detail::Checksum(damaged);
    Expect(
        !DecodeSectionLight(EncodeSectionLight(damaged)) &&
            RestoreSectionLight(incompatible, damaged, catalog, &diagnostic) ==
                SectionLightRestoreResult::Rejected &&
            diagnostic.reason == SectionLightRestoreFailure::Footprint,
        "missing sky/halo dependency fails even with a recomputed payload "
        "checksum");
    auto malformed = encoded;
    malformed["coord"][0] = uint64_t{1} << 40;
    Expect(!DecodeSectionLight(malformed),
           "persisted coordinates cannot narrow");
    malformed = encoded;
    malformed["format_version"] = true;
    Expect(!DecodeSectionLight(malformed),
           "boolean schema version cannot grant disk authority");
    malformed = encoded;
    malformed["inputs"].push_back(malformed["inputs"][0]);
    Expect(!DecodeSectionLight(malformed),
           "duplicate dependency coordinates fail closed");
    malformed = encoded;
    malformed.erase("padded_light");
    Expect(!DecodeSectionLight(malformed),
           "legacy interior bytes are not a padded-field certificate");
    original.SetBlock(source, BLOCK_AIR);
    const auto before_eviction =
        CaptureSectionLight(original, root, refreshed_catalog);
    Expect(before_eviction && before_eviction->checksum == receipt->checksum &&
               RestoreSectionLight(original, *before_eviction,
                                   refreshed_catalog) ==
                   SectionLightRestoreResult::Rejected,
           "export preserves original input contents and reload rejects "
           "changed geometry");
    original.GetChunkManager().RemoveChunk(UChunkManager::WorldToChunk(source));
    original.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return false; });
    const auto after_eviction =
        CaptureSectionLight(original, root, refreshed_catalog);
    Expect(
        after_eviction && after_eviction->checksum == receipt->checksum &&
            !original.HasCompleteLightSamplingCalculation(root,
                                                          refreshed_catalog) &&
            RestoreSectionLight(original, *after_eviction, refreshed_catalog) ==
                SectionLightRestoreResult::InputsUnavailable,
        "retired unknown inputs cannot erase frozen certificate or grant "
        "current readiness");
    original.SetBlock(source, 50);
    original.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const auto restored =
        RestoreSectionLight(original, *after_eviction, refreshed_catalog);
    const auto reexported =
        CaptureSectionLight(original, root, refreshed_catalog);
    Expect(restored == SectionLightRestoreResult::Restored &&
               original.HasCompleteLightSamplingCalculation(
                   root, refreshed_catalog) &&
               reexported && reexported->checksum == receipt->checksum,
           "unchanged reloaded contents restore fresh stamps and retain "
           "reexportable evidence");
    auto raw_bytes =
        original.GetChunkManager().GetSectionLightField(root)->GetData();
    raw_bytes[0] ^= 1;
    original.GetChunkManager().ReplaceSectionLightData(root, raw_bytes);
    Expect(!CaptureSectionLight(original, root, refreshed_catalog),
           "raw field mutation invalidates frozen output evidence");
    auto raw_result =
        UChunkRelightSnapshot::CaptureSections(original, spec, {root})
            .Compute(*registry);
    raw_result.content_certificate.reset();
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *raw_result.chunks[0].padded_light, true, true);
    Expect(
        original.RecordLightCalculation(raw_result, catalog) == 1 &&
            original.HasCompleteLightSamplingCalculation(root, catalog) &&
            !CaptureSectionLight(original, root, catalog),
        "raw legacy result cannot manufacture private frozen content evidence");
    auto substituted =
        UChunkRelightSnapshot::CaptureSections(original, spec, {root})
            .Compute(*registry);
    auto altered_volume = std::make_shared<SectionLightVolume>(
        *substituted.chunks[0].padded_light);
    altered_volume->cells[0] ^= 1;
    substituted.chunks[0].padded_light = altered_volume;
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *altered_volume, true, true);
    Expect(original.RecordLightCalculation(substituted, catalog) == 1 &&
               !CaptureSectionLight(original, root, catalog),
           "borrowed input certificate cannot bless substituted padded output "
           "bytes");
    auto mismatched =
        UChunkRelightSnapshot::CaptureSections(original, spec, {root})
            .Compute(*registry);
    mismatched.read_set.push_back(mismatched.read_set.back());
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *mismatched.chunks[0].padded_light, true, true);
    Expect(original.RecordLightCalculation(mismatched, catalog) == 1 &&
               !CaptureSectionLight(original, root, catalog),
           "borrowed content certificate cannot bless a different runtime read "
           "set");
    for (const bool sky : {true, false})
    {
      auto channel_spec = spec;
      channel_spec.include_skylight = sky;
      channel_spec.include_block_light = !sky;
      const auto channel =
          UChunkRelightSnapshot::CaptureSections(original, channel_spec, {root})
              .Compute(*registry);
      original.GetChunkManager().ReplaceComputedSectionLightVolume(
          root, *channel.chunks[0].padded_light, sky, !sky);
      Expect(original.RecordLightCalculation(channel, catalog) == 1,
             "individual channel records its own private calculated output");
    }
    const auto merged = CaptureSectionLight(original, root, catalog);
    Expect(merged && merged->checksum == receipt->checksum,
           "independent sky and block calculations merge identical frozen "
           "geometry evidence");
    original.SetBlock(root * CHUNK_SIZE + glm::ivec3(8, 8, 8), 8);
    auto block_spec = spec;
    block_spec.include_skylight = false;
    const auto new_block =
        UChunkRelightSnapshot::CaptureSections(original, block_spec, {root})
            .Compute(*registry);
    original.GetChunkManager().ReplaceComputedSectionLightVolume(
        root, *new_block.chunks[0].padded_light, false, true);
    Expect(original.RecordLightCalculation(new_block, catalog) == 1 &&
               original.HasRecordedLightSamplingOutput(root) &&
               !CaptureSectionLight(original, root, catalog),
           "independent channel certificates cannot mix incompatible geometry "
           "contents");
  }
}

int main(int argc, char **argv)
{
  if (argc == 2)
  {
    TestSyncContract(argv[1]);
    std::cout << "sync_light_contract_test: OK\n";
    return 0;
  }
  TestPersistedSectionLight();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  constexpr int kMaxWorldY = cutum::CHUNK_SIZE - 1;
  auto registry = MakeRegistry();
  cutum::UBlockWorld world;
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  const glm::ivec3 chunk_coord(0, 0, 0);

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 15, 8)) == cutum::kMaxLightLevel,
         "open sky at chunk top");

  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, 8, z), kStone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), kStone);
      world.SetBlock(glm::ivec3(x, y, 11), kStone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), kStone);
      world.SetBlock(glm::ivec3(11, y, z), kStone);
    }
  }

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 5, 8)) == 0, "enclosed room skylight");

  world.SetBlock(glm::ivec3(8, 5, 8), kTorch);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 8)) >= 13, "torch emission");
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) > 0, "torch blocklight spread");

  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) == 0,
         "torch blocklight removed after break");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 1; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0, "skylight enters open shaft");
  world.SetBlock(glm::ivec3(8, 2, 8), kStone);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) == 0,
         "skylight blocked by second shaft block");
  world.SetBlock(glm::ivec3(8, 2, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0,
         "skylight restored after shaft reopen");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  BuildEnclosedRoom(world, kStone, 5);
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) == 0,
         "deep room dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) > 0,
         "deep shaft to surface relights room");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), kStone);
  for (int x = 8; x <= 17; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), kStone);
    world.SetBlock(glm::ivec3(x, 6, 8), kStone);
    world.SetBlock(glm::ivec3(x, 4, 8), kStone);
    world.SetBlock(glm::ivec3(x, 5, 7), kStone);
    world.SetBlock(glm::ivec3(x, 5, 9), kStone);
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 4, z), kStone);
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      for (int y = 6; y <= 8; ++y)
      {
        if (x == 11 || x == 13 || z == 7 || z == 9)
        {
          world.SetBlock(glm::ivec3(x, y, z), kStone);
        }
      }
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) == 0,
         "offset cave dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  for (int x = 9; x <= 16; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) > 0,
         "offset cave lit after shaft and tunnel open");

  {
    cutum::UBlockWorld neigh_world;
    neigh_world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    for (int x = 0; x < 32; ++x)
    {
      for (int z = 0; z < 16; ++z)
      {
        neigh_world.SetBlock(glm::ivec3(x, 0, z), kStone);
      }
    }
    neigh_world.SetBlock(glm::ivec3(20, 4, 8), kTorch);
    cutum::RelightChunk(neigh_world, *registry, glm::ivec3(0, 0, 0));
    cutum::RelightChunk(neigh_world, *registry, glm::ivec3(1, 0, 0));
    cutum::RelightJobSpec spec;
    spec.block_positions = {glm::ivec3(8, 0, 8)};
    spec.min_world_y = 0;
    spec.max_world_y = 15;
    spec.column_center_only = true;
    spec.frontier_iterations = 0;
    auto snap = cutum::UChunkRelightSnapshot::Capture(neigh_world, spec);
    Expect(snap.GetCapturedFullChunks() == 1,
           "D: center-only copies one full chunk");
    Expect(snap.HasChunk(glm::ivec3(0, 0, 0)),
           "D: center chunk has blocks");
    Expect(!snap.HasChunk(glm::ivec3(1, 0, 0)),
           "D: neighbor not in Blocks");
    Expect(snap.HasLight(glm::ivec3(1, 0, 0)),
           "D: neighbor has scratch light for geometry-derived propagation");
    Expect(snap.GetCapturedNeighborLightChunks() == 0,
           "D: no neighboring light bytes are dependencies");
    Expect(snap.GetCapturedGeometryHaloChunks() >= 1,
           "D: neighboring geometry is captured");
    auto result = snap.Compute(*registry);
    Expect(result.chunks.size() == 1,
           "D: apply only center light");
    bool center_edge_lit = false;
    for (const auto &chunk_data : result.chunks)
    {
      if (chunk_data.coord != glm::ivec3(0, 0, 0))
      {
        continue;
      }
      const glm::ivec3 local(15, 4, 8);
      const int idx = local.x + cutum::CHUNK_SIZE *
                                    (local.y + cutum::CHUNK_SIZE * local.z);
      const int block_light = (chunk_data.light_packed[idx] >> 4) & 0x0F;
      center_edge_lit = block_light > 0;
    }
    Expect(center_edge_lit, "D: neighbor torch seeds center face");
  }

  {
    constexpr int kTallWorldMaxY = 319;
    cutum::UBlockWorld tall_world;
    tall_world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    const glm::ivec3 torch_pos(8, kTallWorldMaxY - 1, 8);
    tall_world.SetBlock(torch_pos, kTorch);
    cutum::RelightBlocksAroundAll(tall_world, *registry, {torch_pos},
                                  kTallWorldMaxY);
    Expect(BlockAt(tall_world, torch_pos + glm::ivec3(0, 1, 0)) > 0,
           "block light propagates inside the top supported chunk slice");
  }

  {
    constexpr int kMinWorldY = -64;
    cutum::UBlockWorld deep_world;
    deep_world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    deep_world.GetChunkManager().EnsureChunk(glm::ivec3(0, -4, 0));
    deep_world.GetChunkManager().EnsureChunk(glm::ivec3(0, -3, 0));
    const glm::ivec3 torch_pos(8, -49, 8);
    deep_world.SetBlock(torch_pos, kTorch);
    cutum::RelightColumn(deep_world, *registry, torch_pos.x, torch_pos.z,
                         kMinWorldY, -33, true, false);
    Expect(BlockAt(deep_world, torch_pos) >= 13,
           "block light source is processed below world Y=0");
    Expect(BlockAt(deep_world, glm::ivec3(8, -48, 8)) > 0,
           "block light crosses the negative chunk seam at Y=-48");
  }

  // E: one union flood per batch must reach the same fixed point the
  // per-chunk floods reached. Three stacked chunks, two torches in separate
  // air shafts; assert exact max-plus attenuation across both chunk seams.
  {
    cutum::UBlockWorld stack_world;
    stack_world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    for (int y = 0; y < 48; ++y)
    {
      for (int x = 0; x < 16; ++x)
      {
        for (int z = 0; z < 16; ++z)
        {
          stack_world.SetBlock(glm::ivec3(x, y, z), kStone);
        }
      }
    }
    for (int y = 0; y < 48; ++y)
    {
      stack_world.SetBlock(glm::ivec3(4, y, 4), cutum::BLOCK_AIR);
      stack_world.SetBlock(glm::ivec3(12, y, 12), cutum::BLOCK_AIR);
    }
    const glm::ivec3 torch_a(4, 6, 4);
    const glm::ivec3 torch_b(12, 26, 12);
    stack_world.SetBlock(torch_a, kTorch);
    stack_world.SetBlock(torch_b, kTorch);

    cutum::RelightJobSpec spec;
    spec.block_positions = {glm::ivec3(8, 0, 8)};
    spec.min_world_y = 0;
    spec.max_world_y = 47;
    spec.column_center_only = true;
    spec.frontier_iterations = 0;
    auto snap = cutum::UChunkRelightSnapshot::Capture(stack_world, spec);
    Expect(snap.GetCapturedFullChunks() == 3,
           "E: center-only copies the three-band stack");
    auto result = snap.Compute(*registry);

    auto block_light_at = [&result](glm::ivec3 pos)
    {
      const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
      for (const auto &chunk_data : result.chunks)
      {
        if (chunk_data.coord != chunk_coord)
        {
          continue;
        }
        const glm::ivec3 local = cutum::UChunkManager::WorldToLocal(pos);
        const int idx = local.x + cutum::CHUNK_SIZE *
                                       (local.y + cutum::CHUNK_SIZE * local.z);
        return (chunk_data.light_packed[idx] >> 4) & 0x0F;
      }
      return -1;
    };

    // Torch A at (4,6,4), emission 14, straight air shaft up: 14 - dy.
    Expect(block_light_at(glm::ivec3(4, 15, 4)) == 5,
           "E: torch A lights bottom-chunk seam face");
    Expect(block_light_at(glm::ivec3(4, 16, 4)) == 4,
           "E: torch A crosses seam into middle chunk");
    Expect(block_light_at(glm::ivec3(4, 19, 4)) == 1,
           "E: torch A reaches attenuation tail in middle chunk");
    Expect(block_light_at(glm::ivec3(4, 20, 4)) == 0,
           "E: torch A exhausts at range");
    // Torch B at (12,26,12): crosses the
    // middleР В Р вЂ Р Р†Р вЂљР’В Р Р†Р вЂљРІвЂћСћtop seam.
    Expect(block_light_at(glm::ivec3(12, 31, 12)) == 9,
           "E: torch B lights middle-chunk seam face");
    Expect(block_light_at(glm::ivec3(12, 32, 12)) == 8,
           "E: torch B crosses seam into top chunk");
    // Both shafts converge in the shared flood without starving either seed.
    Expect(block_light_at(torch_a) == 14, "E: torch A keeps source emission");
    Expect(block_light_at(torch_b) == 14, "E: torch B keeps source emission");
  }

  // Exercise public synchronous entry points with profiles that share neither
  // the default bottom nor the default top. Known absent AIR is explicit.
  for (const auto profile : {cutum::WorldVerticalProfile(-127, -17, -48, -127),
                             cutum::WorldVerticalProfile(32, 79, 48, 32)})
    for (const int y : {profile.MinY + 1, profile.MaxY - 1})
    {
      cutum::UBlockWorld custom;
      custom.SetVerticalProfile(profile);
      custom.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      const glm::ivec3 source(8, y, 8);
      custom.SetBlock(source, kTorch);
      const auto coord = cutum::UChunkManager::WorldToChunk(source);
      cutum::RelightChunkBlockLight(custom, *registry, coord);
      Expect(BlockAt(custom, source) == 14 &&
                 BlockAt(custom, source + glm::ivec3(1, 0, 0)) == 13,
             "sync blocklight must work at both edges of a nondefault profile");
      custom.SetBlock(source, cutum::BLOCK_AIR);
      cutum::RelightBlocksAroundEdit(custom, *registry, {source});
      Expect(BlockAt(custom, source + glm::ivec3(1, 0, 0)) == 0,
             "source removal must clear light inside the configured height band");
      Expect(!custom.GetChunkManager().HasChunk(
                 {0, profile.MinChunkY(cutum::CHUNK_SIZE)-1, 0}) &&
                 !custom.GetChunkManager().HasChunk(
                 {0, profile.MaxChunkY(cutum::CHUNK_SIZE)+1, 0}),
             "relighting must not materialize slices beyond the configured profile");
    }

  // A partial relight at negative Y must use the entire vertical sky context,
  // including attenuation in read-only water above its writable slice.
  cutum::SetTestSkylightSeedAvailable(true);
  for (const bool roof : {true, false})
  {
    cutum::UBlockWorld deep;
    deep.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
    for (int x = -16; x < 32; ++x)
      for (int z = -16; z < 32; ++z)
      {
        deep.SetBlock({x, -48, z}, kStone);
        if (roof)
          deep.SetBlock({x, 304, z}, kStone);
        else
          for (int y = -32; y <= -30; ++y)
            deep.SetBlock({x, y, z}, 9);
      }
    const int expected = roof ? 0 : 12;
    cutum::RelightChunk(deep, *registry, {0, -3, 0});
    Expect(SkyAt(deep, {8, -33, 8}) == expected,
           roof ? "sync sky sees roof more than 240 blocks above negative slice"
                : "sync sky retains attenuation across water slice boundary");
    cutum::UGpuFullLightingPipeline desktop_pipeline;
    desktop_pipeline.RelightChunk(deep, *registry, {0, -3, 0});
    Expect(SkyAt(deep, {8, -33, 8}) == expected,
           roof
               ? "desktop pipeline retains full-height roof with seed available"
               : "desktop pipeline retains incoming water attenuation");
    cutum::RelightJobSpec spec;
    spec.block_positions.push_back({8, -48, 8});
    spec.min_world_y = -48;
    spec.max_world_y = -33;
    spec.column_center_only = true;
    spec.include_block_light = false;
    auto snapshot = cutum::UChunkRelightSnapshot::Capture(deep, spec);
    const auto result = snapshot.Compute(*registry);
    Expect(result.chunks.size() == 1,
           "read-only full-height sky context does not expand writable band");
    const int idx = cutum::UChunk::LocalIndex({8, 15, 8});
    Expect((result.chunks.front().light_packed[idx] & 0x0f) == expected,
           roof ? "async sky sees full-height opaque roof"
                : "async sky retains cross-slice liquid attenuation");
    if (roof)
    {
      bool roof_recorded = false;
      for (const auto &stamp : result.read_set)
        roof_recorded |= stamp.coord == glm::ivec3(0, 19, 0) &&
                         stamp.readsContent && !stamp.readsLight;
      Expect(roof_recorded,
             "far roof geometry participates in async result validation");
    }
  }
  cutum::SetTestSkylightSeedAvailable(false);

  std::cout << "chunk_lighting_propagation_test: OK" << std::endl;
  return 0;
}
