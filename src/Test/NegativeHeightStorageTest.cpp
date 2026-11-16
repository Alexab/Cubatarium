#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
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
