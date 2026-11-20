#pragma once
#include "App/Core.h"
#include "App/WorldOperationRunner.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Progress/IUProgressSink.h"
#include "World/Core/World.h"
#include "World/Core/RuntimeTuning.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Physics/WorldChunkDirtyService.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Lighting/IULightingPipeline.h"
#include "World/Lighting/AsyncRelightBuilder.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

namespace cutum
{
// Opt-in native API probe. All fixtures are new subdirectories of a previously
// nonexistent output directory; no live world, config or source save is edited.
inline void RunTerrainSaveFailureProbe(UCore &core)
{
  const char *output = std::getenv("CUBA_FLIGHT_QA_TERRAIN_SAVE");
  if (!output || !*output)
    return;
  namespace fs = std::filesystem;
  using Json = nlohmann::json;
  const auto root = fs::absolute(output).lexically_normal();
  if (fs::exists(root))
    throw std::runtime_error("Terrain save probe refuses existing output");
  fs::create_directories(root);
  Json report = {
      {"schema", "terrain-save-failure-native.v1"},
      {"scope", "Actual isolated UWorld cooperative saves, Core full-save "
                "facade and WorldOperationRunner; no application menu/error "
                "rendering or autosave scheduler acceptance"},
      {"status", "FAIL"},
      {"cases", Json::array()}};
  const auto require = [](bool value, const char *message)
  {
    if (!value) throw std::runtime_error(message);
  };
  const auto capture = [](const fs::path &folder)
  {
    std::map<std::string, std::vector<uint8_t>> files;
    for (const auto &entry : fs::recursive_directory_iterator(folder))
      if (entry.is_regular_file())
      {
        std::ifstream file(entry.path(), std::ios::binary);
        files[entry.path().lexically_relative(folder).generic_string()] =
            std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
      }
    return files;
  };
  struct Progress final : IUProgressSink
  {
    Json phases = Json::array();
    void Begin(WorldOperationKind) override {}
    void End(bool, const std::string &) override {}
    void Report(const std::string &phase, float, const std::string &, int, int) override
    {
      if (phases.empty() || phases.back() != phase) phases.push_back(phase);
    }
  };
  try
  {
    // S4.01: section physical truth versus surviving coarse column state.
    {
      UWorld world(nullptr,nullptr);
      world.ProceduralTemplate.MinHeight=-48;
      world.ProceduralTemplate.MaxHeight=-33;
      world.ProceduralTemplate.SeaLevel=-40;
      world.BlockWorld.SetVerticalProfile({-48,-33,-40});
      (void)world.GetLightingPipeline();
      auto definitions=std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone,torch;
      stone.Physics=BlockPhysicsProfile::Solid(); stone.Name="stone";
      torch.Render.Style=BlockRenderStyle::Cross; torch.Name="torch"; torch.Lighting.Emission=14;
      definitions->ReplaceAll({{8,stone},{50,torch}},{{"stone",8},{"torch",50}});
      world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
      const glm::ivec3 coord(0,-3,0);
      const auto populate=[&] {
        for(int x=0;x<16;++x) for(int z=0;z<16;++z) world.BlockWorld.SetBlock({x,-48,z},8);
        world.BlockWorld.SetBlock({8,-40,8},50);
      };
      const auto settle=[&] {
        RelightJobSpec spec; spec.min_world_y=-48; spec.max_world_y=-33;
        auto snapshot=UChunkRelightSnapshot::CaptureSections(world.BlockWorld,spec,{coord});
        snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
        const auto result=snapshot.Compute(*world.BlockRegistry);
        require(result.inputs_complete && result.chunks.size()==1,"section owner fixture complete calculation");
        InstallComputedLight(world.BlockWorld,result.chunks[0],true,true);
        require(world.BlockWorld.RecordLightCalculation(result,
                    world.BlockRegistry->GetDefinitionsCatalogSnapshot())==1,
                "section owner fixture records physical certificate");
        world.NoteChunkSliceLightCalculationSettled(coord);
      };
      populate(); settle();
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::LitReady);
      require(world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  world.HasCurrentChunkSliceLightSettlement(coord) && world.IsColumnLitReady({0,0,0}),
              "section owner positive ready precondition");
      world.BlockWorld.SetBlock({2,-40,2},8);
      report["section_owner_checkpoint"]={
          {"physical",world.HasCurrentColumnLightCoverage({0,0},-48,-33)},
          {"section_settlement",world.HasCurrentChunkSliceLightSettlement(coord)},
          {"column_ready",world.IsColumnLitReady({0,0,0})},
          {"coarse_state",static_cast<int>(world.GetColumnEmergeState({0,0,0}))},
          {"pending",world.IsPendingLightBeforeMesh({0,0})}};
      require(!world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  !world.HasCurrentChunkSliceLightSettlement(coord) && !world.IsColumnLitReady({0,0,0}),
              "coarse column cannot certify geometry-invalidated section");
      UColumnFlowExecutor section_executor;
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      report["section_owner_job_stage_checkpoint"]={
          {"stage",ColumnJobStageName(section_executor.GetColumnJobStage({0,0}))},
          {"pending_light",world.GetColumnRecords().GetOrCreate({0,0}).pending_light},
          {"physical",world.HasCurrentColumnLightCoverage({0,0},-48,-33)}};
      require(world.GetColumnRecords().GetOrCreate({0,0}).pending_light &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::PendingLight,
              "executor retains uncertified section light debt without queue metadata");
      settle();
      require(world.IsColumnLitReady({0,0,0}),"section owner recompute restores aggregate");
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      require(!world.GetColumnRecords().GetOrCreate({0,0}).pending_light &&
                  section_executor.GetColumnJobStage({0,0})!=ColumnJobStage::PendingLight,
              "executor clears physical light debt after current calculation");
      const auto previous_incarnation=world.BlockWorld.GetChunkManager().GetChunk(coord)->GetIncarnation();
      world.BlockWorld.GetChunkManager().RemoveChunk(coord);
      populate(); world.BlockWorld.SetBlock({2,-40,2},8);
      require(world.BlockWorld.GetChunkManager().GetChunk(coord)->GetIncarnation()!=previous_incarnation &&
                  !world.HasCurrentChunkSliceLightSettlement(coord) && !world.IsColumnLitReady({0,0,0}),
              "old section incarnation cannot confirm replacement aggregate");
      settle();
      const auto revision=world.BlockWorld.GetChunkManager().GetChunk(coord)->GetLightFieldRevision();
      world.NoteChunkSliceLightCalculationSettled(coord);
      world.NoteChunkSliceLightCalculationSettled(coord);
      require(world.IsColumnLitReady({0,0,0}) &&
                  world.BlockWorld.GetChunkManager().GetChunk(coord)->GetLightFieldRevision()==revision,
              "repeated settlement preserves current field identity");
      require(!world.IsColumnLitReady({7,0,7}),"unknown coarse Empty is not physical readiness");
      // An unfinished different slice must not reopen an already-current band.
      world.ProceduralTemplate.MaxHeight=-17;
      auto wider=world.BlockWorld.GetVerticalProfile(); wider.MaxY=-17;
      world.BlockWorld.SetVerticalProfile(wider);
      world.BlockWorld.SetBlock({4,-24,4},8);
      settle();
      require(world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  !world.IsColumnLitReady({0,0,0}),
              "section owner mixed-band precondition");
      require(!world.Persistence->TryEnqueueTerrainColumnRelight(world,0,0,false,-48,-33) &&
                  !world.IsTerrainColumnRelightQueued({0,0}),
              "current band is not requeued because a different slice is unfinished");
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::Meshing);
      const bool promoted=UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor);
      report["section_owner_publication_checkpoint"]={
          {"lower_current",world.HasCurrentColumnLightCoverage({0,0},-48,-33)},
          {"upper_current",world.HasCurrentColumnLightCoverage({0,0},-32,-17)},
          {"pending",world.IsPendingLightBeforeMesh({0,0})},
          {"promoted",promoted},
          {"coarse_state",static_cast<int>(world.GetColumnEmergeState({0,0,0}))},
          {"stage",ColumnJobStageName(section_executor.GetColumnJobStage({0,0}))}};
      require(!promoted && world.GetColumnEmergeState({0,0,0})!=ColumnEmergeState::RenderReady,
              "lit publication cannot promote column with uncertified sibling");
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::LitReady);
      require(!UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor),
              "coarse LitReady also cannot bypass uncertified sibling");
      RelightJobSpec all_bands;
      all_bands.min_world_y=-48; all_bands.max_world_y=-17;
      auto full_snapshot=UChunkRelightSnapshot::CaptureSections(
          world.BlockWorld,all_bands,{coord,{0,-2,0}});
      full_snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto full_result=full_snapshot.Compute(*world.BlockRegistry);
      require(full_result.inputs_complete && full_result.chunks.size()==2,
              "publication aggregate full-column calculation precondition");
      for(const auto &chunk:full_result.chunks)
        InstallComputedLight(world.BlockWorld,chunk,true,true);
      require(world.BlockWorld.RecordLightCalculation(full_result,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot())==2,
              "publication aggregate records both section certificates");
      world.NoteChunkSliceLightCalculationSettled(coord);
      world.NoteChunkSliceLightCalculationSettled({0,-2,0});
      require(world.IsColumnLitReady({0,0,0}) &&
                  UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor) &&
                  world.GetColumnEmergeState({0,0,0})==ColumnEmergeState::RenderReady &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
              "current column light aggregate permits publication notification promotion");
      report["cases"].push_back({{"kind","section_owner_column_aggregate"},{"status","PASS"},
          {"stale_geometry_rejected",true},{"replacement_rejected",true},
          {"recompute_recovers",true},{"repeat_idempotent",true},{"band_local_noop",true},
          {"executor_retains_physical_debt",true},{"executor_settle_clears_debt",true},
          {"publication_rejects_uncertified_sibling",true},
          {"publication_current_light_recovers",true}});
    }
    // S3.06: actual factory-selected desktop owner on the app's GL context.
    {
      UWorld world(nullptr, nullptr);
      world.BlockWorld.SetVerticalProfile({-48,47,-17});
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, torch;
      stone.Name = "stone"; stone.Physics = BlockPhysicsProfile::Solid();
      torch.Name = "torch"; torch.Render.Style = BlockRenderStyle::Cross;
      torch.Lighting.Emission = 14;
      definitions->ReplaceAll({{8,stone},{50,torch}},{{"stone",8},{"torch",50}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr,definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
      world.BlockWorld.SetBlock({-1,-40,-8},50);
      for(int x=-32;x<16;++x)
        for(int z=-32;z<16;++z) world.BlockWorld.SetBlock({x,32,z},8);
      auto &pipeline = world.GetLightingPipeline();
      RelightJobSpec spec;
      spec.min_world_y=-48; spec.max_world_y=47;
      auto snapshot=UChunkRelightSnapshot::CaptureSections(
          world.BlockWorld,spec,{{-1,-3,-1}});
      snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto expected=snapshot.Compute(*world.BlockRegistry);
      require(expected.inputs_complete && expected.chunks.size()==1,
              "desktop GPU owner probe has complete CPU reference");
      const auto before=GpuLightTransportDispatchCount();
      pipeline.RelightChunk(world.BlockWorld,*world.BlockRegistry,{-1,-3,-1},true,true);
      const auto dispatched=GpuLightTransportDispatchCount()-before;
      const auto field=world.BlockWorld.GetChunkManager().GetSectionLightField({-1,-3,-1});
      require(dispatched==15 && field && field->GetPaddedData() &&
                  field->GetPaddedData()->cells==expected.chunks[0].padded_light->cells &&
                  world.BlockWorld.HasCompleteLightSamplingCalculation(
                      {-1,-3,-1},world.BlockRegistry->GetDefinitionsCatalogSnapshot()),
              "app desktop owner must dispatch real GPU and publish exact certified field");
      require(SyncRelightTransport==nullptr,"app desktop owner restores transport scope");
      report["cases"].push_back({{"kind","desktop_gpu_relight_owner"},
          {"status","PASS"},{"actual_dispatches",dispatched},
          {"cpu_padded_bytes_match",true},{"current_certificate",true},
          {"scope_restored",true}});
    }
    // S3.04: real World lifecycle; no live player/world or disk data involved.
    {
      UWorld world(nullptr, nullptr);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
      world.NextAsyncRelightJobId = 9200;
      RelightJobSpec spec;
      spec.job_id = 9199;
      spec.block_positions = {{4, -40, 4}};
      spec.min_world_y = -48;
      spec.max_world_y = -33;
      spec.column_center_only = true;
      spec.include_known_air_outputs = true;
      spec.known_air_output_sections = {{1, -2, 0}};
      spec.light_sample_output_sections = {{0, -3, 0}};
      spec.finalize_pending_gate = false;
      spec.visible_draw_gate_repair = true;
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                  world.AsyncRelight->GetCompletedSize() == 1,
              "relight lifecycle completed predecessor precondition");
      world.QuiesceBackgroundWork(std::chrono::milliseconds(100));
      require(world.DeferredRelightRetries.Size() == 1 &&
                  world.DeferredRelightRetries.IsSuspended() &&
                  world.DrainAsyncRelightResults(4, false, false) == 0 &&
                  world.AsyncRelight->GetInFlightCount() == 0 &&
                  world.BlockWorld.GetChunkManager().GetSectionLightFieldCount() == 0,
              "quiesce retains exact debt without dispatch/publication");
      world.ResumeAfterSessionSave();
      bool exact = false;
      world.DeferredRelightRetries.Drain(1, [&](const RelightJobSpec &retry) {
        exact = retry.job_id == spec.job_id &&
                retry.block_positions == spec.block_positions &&
                retry.min_world_y == spec.min_world_y &&
                retry.max_world_y == spec.max_world_y &&
                retry.column_center_only == spec.column_center_only &&
                retry.include_known_air_outputs == spec.include_known_air_outputs &&
                retry.known_air_output_sections == spec.known_air_output_sections &&
                retry.light_sample_output_sections == spec.light_sample_output_sections &&
                retry.include_skylight == spec.include_skylight &&
                retry.include_block_light == spec.include_block_light &&
                retry.frontier_iterations == spec.frontier_iterations &&
                retry.finalize_pending_gate == spec.finalize_pending_gate &&
                retry.visible_draw_gate_repair == spec.visible_draw_gate_repair;
        return false; // Observe the actual retained request without consuming it.
      });
      require(exact, "World preserves every retry specification field");
      world.DrainAsyncRelightResults(4, false, false);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "resumed relight successor worker bound");
      world.DrainAsyncRelightResults(4, false, false);
      require(world.DeferredRelightRetries.Empty() &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({0, -3, 0}) &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({1, -2, 0}),
              "resumed World installs certified AIR and out-of-band target");
      const auto sample = world.BlockWorld.SampleSceneLight({20, -24, 4});
      require(sample.packed && UnpackSky(*sample.packed) == 15,
              "out-of-band AIR receives physical open sky");
      spec.job_id = 9300;
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "replacement predecessor worker bound");
      world.CancelAsyncRelightWork();
      require(world.DeferredRelightRetries.Size() == 1,
              "replacement fixture contains old-world debt");
      world.AbandonTerrainForWorldReplace();
      require(world.DeferredRelightRetries.Empty() &&
                  world.BlockWorld.GetChunkManager().GetSectionLightFieldCount() == 0,
              "world replacement retires old debt and fields");
      report["cases"].push_back({{"kind", "relight_cancel_quiesce_resume"},
          {"status", "PASS"}, {"exact_spec_retained", true},
          {"no_dispatch_during_quiesce", true}, {"certified_air_successor", true},
          {"outside_band_target", true}, {"replacement_retires_debt", true}});
    }
    // Fixed S3.04 install matrix: mutate only after the worker has completed.
    for (const std::string mutation : {"read_set", "catalog", "profile",
                                       "configuration", "incarnation"})
    {
      UWorld world(nullptr, nullptr);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      auto &manager = world.BlockWorld.GetChunkManager();
      manager.EnsureChunk({1, -3, 0});
      const auto old_incarnation = manager.GetChunk({1, -3, 0})->GetIncarnation();
      world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
      world.NextAsyncRelightJobId = 9400;
      RelightJobSpec spec;
      spec.job_id = 9399;
      spec.block_positions = {{4, -40, 4}};
      spec.min_world_y = -48;
      spec.max_world_y = -33;
      spec.column_center_only = true;
      spec.include_known_air_outputs = true;
      spec.light_sample_output_sections = {{0, -3, 0}};
      spec.finalize_pending_gate = false;
      auto sync_snapshot = UChunkRelightSnapshot::Capture(world.BlockWorld, spec);
      sync_snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto sync_result = sync_snapshot.Compute(*world.BlockRegistry);
      require(ValidateRelightInputs(sync_result, world.BlockWorld,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot(), true) ==
                  RelightInputValidation::Current,
              "stale matrix requires current complete original inputs");
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                  world.AsyncRelight->GetCompletedSize() == 1,
              "stale matrix completed worker precondition");
      auto expected = RelightInputValidation::ReadSet;
      if (mutation == "read_set")
        world.BlockWorld.SetBlock({16, -40, 4}, 8);
      else if (mutation == "catalog")
      {
        stone.Lighting.Emission = 7;
        definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
        expected = RelightInputValidation::Catalog;
      }
      else if (mutation == "profile")
      {
        profile.MaxY = -1;
        world.BlockWorld.SetVerticalProfile(profile);
        expected = RelightInputValidation::Profile;
      }
      else if (mutation == "configuration")
      {
        world.BlockWorld.SetLightingConfigurationIdentity(99);
        expected = RelightInputValidation::Configuration;
      }
      else
      {
        manager.RemoveChunk({1, -3, 0});
        manager.EnsureChunk({1, -3, 0});
        require(manager.GetChunk({1, -3, 0})->GetIncarnation() != old_incarnation,
                "incarnation control must replace equal AIR geometry");
      }
      require(ValidateRelightInputs(sync_result, world.BlockWorld,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot(), true) == expected,
              "immutable snapshot must reject the exact changed input category");
      const int old_applied = world.DrainAsyncRelightResults(1, false, false);
      require(old_applied == 0 && world.NextAsyncRelightJobId == 9401 &&
                  !manager.GetSectionLightField({0, -3, 0}),
              "World rejects stale completion before publishing and enqueues successor");
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "stale successor worker bound");
      require(world.DrainAsyncRelightResults(1, false, false) == 1 &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({0, -3, 0}) &&
                  !world.HasPendingAsyncRelightWork(),
              "World stale successor installs current certified AIR field");
      report["cases"].push_back({{"kind", "relight_stale_" + mutation},
          {"status", "PASS"}, {"validation_reason", static_cast<int>(expected)},
          {"stale_applied", old_applied}, {"successor_job", 9401},
          {"certified_air_successor", true}});
    }
    // S3.05: upper async persistence owner, with actual disk worker results.
    for (const std::string cache_case : {"compatible", "materials", "legacy"})
    {
      const auto folder = root / ("persisted-load-" + cache_case);
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      world.ProceduralTemplate.SeaLevel = -33;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      profile.SeaLevelY = -33;
      world.BlockWorld.SetVerticalProfile(profile);
      const auto lighting_identity =
          static_cast<uint64_t>(world.GetLightingPipeline().GetMode());
      require(world.RequiresLightingLitGate() &&
                  world.BlockWorld.GetLightingConfigurationIdentity() == lighting_identity,
              "upper persistence fixture binds the actual Full lighting domain");
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
      const auto bind = [&] {
        definitions->ReplaceAll({{8, stone}, {50, torch}, {9, water}},
            {{"stone", 8}, {"torch", 50}, {"water", 9}});
      };
      bind();
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.SetWorldFolderPath(folder.string());
      world.Streaming->EnsureStreamer(world.BlockWorld, *world.BlockRegistry,
                                      42, world.ProceduralTemplate);
      require(world.Streaming->HasStreamer() &&
                  std::max(world.ProceduralTemplate.MinHeight,
                      world.ProceduralTemplate.SeaLevel - CHUNK_SIZE * 2) <=
                      world.ProceduralTemplate.MaxHeight,
              "upper persistence requires streamer and valid fallback band");
      auto &storage = world.GetChunkStorage();
      UBlockWorld original;
      original.SetVerticalProfile(profile);
      original.SetLightingConfigurationIdentity(lighting_identity);
      original.SetFluidDefinitions(definitions.get());
      original.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      // A procedural column without decoded AIR slices is complete only when
      // every XZ cell has terrain. Keep a real floor across both columns.
      for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 16; ++z)
          original.SetBlock({x, -48, z}, 8);
      original.SetBlock({16, -24, 4}, 50);
      original.SetBlock({2, -47, 2}, 9);
      const auto fluid = FluidCellState::Flowing(3, true).WithKind(FluidKind::Water);
      original.SetFluidState({2, -47, 2}, fluid);
      RelightJobSpec spec;
      spec.min_world_y = -48;
      spec.max_world_y = -17;
      spec.include_known_air_outputs = true;
      const auto computed = UChunkRelightSnapshot::CaptureSections(
          original, spec, {{0, -2, 0}}).Compute(*world.BlockRegistry);
      require(IsTerrainChunkComplete(original, {0, 0, 0}, -17,
                  kNoTerrainChunkSlice, -48) &&
                  IsTerrainChunkComplete(original, {1, 0, 0}, -17,
                  kNoTerrainChunkSlice, -48),
              "upper persistence source columns must satisfy terrain completeness");
      require(computed.inputs_complete && computed.chunks.size() == 1,
              "upper persistence fixture requires complete actual calculation");
      original.GetChunkManager().ReplaceComputedSectionLightVolume(
          {0, -2, 0}, *computed.chunks[0].padded_light, true, true);
      require(original.RecordLightCalculation(computed,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot()) == 1 &&
                  storage.SaveTerrainColumn({0, 0, 0}, original, folder.string(),
                      *world.BlockRegistry, -17, -48) &&
                  storage.SaveTerrainColumn({1, 0, 0}, original, folder.string(),
                      *world.BlockRegistry, -17, -48),
              "upper persistence fixture saves terrain and certified AIR cache");
      if (cache_case == "legacy")
        fs::remove(UChunkStorageService::ColumnLightFilePath(folder.string(), {0, 0, 0}));
      if (cache_case == "materials")
      {
        torch.Lighting.Emission = 6;
        bind();
      }
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([&](glm::ivec3 coord) {
        return storage.IsChunkSliceAbsentOnDisk(folder.string(), coord);
      });
      world.Persistence->RequestAsyncTerrainColumnLoad(world, {1, 0, 0}, 9501);
      world.Persistence->RequestAsyncTerrainColumnLoad(world, {0, 0, 0}, 9502);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!world.Persistence->TickDrainAsyncChunkIo(world, 4))
      {
        require(std::chrono::steady_clock::now() < deadline,
                "upper persistence load worker/drain deadline");
        std::this_thread::yield();
      }
      const auto catalog = world.BlockRegistry->GetDefinitionsCatalogSnapshot();
      require(world.BlockWorld.GetBlock({16, -24, 4}) == 50 &&
                  world.BlockWorld.GetBlock({0, -48, 0}) == 8 &&
                  world.BlockWorld.GetBlock({2, -47, 2}) == 9 &&
                  PackFluidCellState(world.BlockWorld.GetFluidState({2, -47, 2})) == PackFluidCellState(fluid),
              "upper persistence preserves terrain and packed fluid state");
      if (cache_case == "compatible")
        require(world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog) &&
                    world.BlockWorld.GetChunkManager().GetSectionLightField({0, -2, 0})->GetPaddedData()->cells ==
                        computed.chunks[0].padded_light->cells,
                "upper persistence restores compatible AIR cache exactly");
      else
      {
        report["persisted_fallback_checkpoint"] = {
            {"case", cache_case},
            {"column_state", static_cast<int>(world.GetColumnEmergeState({0, 0, 0}))},
            {"coarse_lit_ready", world.IsColumnLitReady({0, 0, 0})},
            {"physical_coverage", world.HasCurrentColumnLightCoverage({0, 0}, -48, -17)},
            {"queued", world.IsTerrainColumnRelightQueued({0, 0})},
            {"noop_skipped", world.GetPhysicsTelemetry().RelightSkippedNoOpEnqueueN}};
        require(!world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog) &&
                    world.IsTerrainColumnRelightQueued({0, 0}),
                "upper persistence incompatible/legacy cache retains ordinary relight obligation");
        world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
        spec.block_positions = {{4, -24, 4}};
        spec.light_sample_output_sections = {{0, -2, 0}};
        spec.column_center_only = true;
        spec.finalize_pending_gate = false;
        world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
        require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                    world.DrainAsyncRelightResults(1, false, false) == 1 &&
                    world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog),
                "upper persistence cache fallback installs physical certified successor");
      }
      const auto field = world.BlockWorld.GetChunkManager().GetSectionLightField({0, -2, 0});
      require(field && field->GetPaddedData() &&
                  UnpackBlock(field->GetPaddedData()->cells[SectionLightVolume::Index(15, 8, 4)]) ==
                      (cache_case == "materials" ? 5 : 13),
              "upper persistence field reflects actual current emission");
      report["cases"].push_back({{"kind", "persisted_world_load_" + cache_case},
          {"status", "PASS"}, {"real_async_load_owner", true},
          {"terrain_fluid_preserved", true}, {"certified_field", true}});
    }
    // Test receipt delivery, not the shore-sealing algorithm or GPU scan.
    // A dry cached slice is the correct initial value even when the normal
    // CPU hitch gate returns empty; no backend result is fabricated here.
    const bool original_gpu_scan = PreferGpuFluidColumnScan();
    bool seal_maps_invalidated = true;
    for (bool near_column : {false, true})
    {
      struct RestoreScanPreference
      {
        bool Previous{PreferGpuFluidColumnScan()};
        ~RestoreScanPreference() { SetPreferGpuFluidColumnScan(Previous); }
      } restore_scan_preference;
      SetPreferGpuFluidColumnScan(false);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto &cache = world.MeshService->GetCache();
      const glm::ivec3 origin(100 * CHUNK_SIZE, 0, 100 * CHUNK_SIZE);
      const std::vector<glm::ivec3> cells{
          origin + glm::ivec3(15, -33, 15),
          origin + glm::ivec3(16, -32, 15),
          origin + glm::ivec3(15, -33, 16)};
      const std::vector<glm::ivec3> columns{{100, 0, 100}, {101, 0, 100},
                                             {100, 0, 101}, {101, 0, 101}};
      for (const auto ground : columns)
      {
        world.BlockWorld.SetBlock({ground.x * CHUNK_SIZE, -48, ground.z * CHUNK_SIZE}, 8);
        const auto *slice = cache.GetFluidSurfaceSlice(world.BlockWorld,
            *world.BlockRegistry, ground, -33);
        require(slice && !slice->HasSurface(15, 15), "seal fixture initial dry cached slice");
        require(cache.GetFluidSurfaceSlice(world.BlockWorld, *world.BlockRegistry,
                    ground, -33) == slice, "seal fixture warm cache hit");
        require(!cache.GetFluidSurfaceDirtyGroundChunks().count(ground),
                "seal fixture must start with settled cache");
      }
      world.ModifiedChunks.clear();
      std::vector<glm::ivec3> changed;
      for (const auto cell : cells)
      {
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Source().WithKind(FluidKind::Water));
        changed.push_back(UChunkManager::WorldToChunk(cell));
      }
      changed.push_back(changed.front()); // real producer receipts may coalesce
      world.Streaming->PublishFluidSealMutations(world, changed, near_column);
      bool invalidated = true, saved = world.ModifiedChunks.size() == cells.size();
      bool no_mesh = true;
      for (const auto cell : cells)
      {
        const auto coord = UChunkManager::WorldToChunk(cell);
        invalidated &= cache.GetFluidSurfaceDirtyGroundChunks().count({coord.x, 0, coord.z}) == 1;
        saved &= world.ModifiedChunks.count(coord) == 1;
        no_mesh &= !cache.HasGreedyMesh(coord);
      }
      const bool control_untouched = !cache.GetFluidSurfaceDirtyGroundChunks().count(columns.back());
      const bool light_obligation = world.GetPendingLightBeforeMeshCount() >= cells.size();
      const bool passed = invalidated && saved && no_mesh && control_untouched && light_obligation;
      report["cases"].push_back({
          {"kind", near_column ? "seal_map_near" : "seal_map_far"},
          {"status", passed ? "PASS" : "FAIL"}, {"cached_dry_columns", columns.size()},
          {"dirty_map_before_remesh", invalidated}, {"actual_owners_saved", saved},
          {"control_untouched", control_untouched}, {"no_mesh_published", no_mesh},
          {"light_obligation_retained", light_obligation},
          {"scope", "Actual shore-seal receipt owner, cached dry map invalidation before visual work; not seal algorithm, surface recompute, or GPU/framebuffer parity"}});
      seal_maps_invalidated &= passed;
    }
    report["fluid_scan_preference_restored"] = PreferGpuFluidColumnScan() == original_gpu_scan;
    require(report["fluid_scan_preference_restored"].get<bool>(), "seal probe changed live scan preference");
    require(seal_maps_invalidated, "shore-seal receipt did not invalidate cached fluid map");
    // Physics owns persistence independently of the best-effort visual queue.
    // These are raw API fixtures, not persisted procedural-profile fixtures.
    bool fluid_dirty_retained = true;
    for (bool eviction : {false, true})
    {
      const auto folder = root / (eviction ? "fluid-visual-eviction" : "fluid-visual-rejection");
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -33;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -33;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto service = std::make_unique<UWorldChunkDirtyService>();
      auto *dirty_service = service.get();
      PhysicsBudgets budgets;
      budgets.VisualRemeshPerTickMax = 0;
      budgets.VisualRemeshQueueSoftLimit = eviction ? 1 : 0;
      budgets.VisualRemeshQueueHardLimit = eviction ? 1 : 0;
      dirty_service->SetBudgets(budgets);
      world.ChunkDirtyService = std::move(service);
      const int columns = eviction ? 2 : 1;
      for (int cx = 0; cx < columns; ++cx)
        for (int x = 0; x < CHUNK_SIZE; ++x)
          for (int z = 0; z < CHUNK_SIZE; ++z)
            world.BlockWorld.SetBlock({cx * CHUNK_SIZE + x, -48, z}, 8);
      world.ModifiedChunks.clear();
      bool before_drain = true;
      for (int cx = 0; cx < columns; ++cx)
      {
        const glm::ivec3 cell(cx * CHUNK_SIZE + 8, -40, 8);
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        world.MarkFluidChangeDirty(cell);
        before_drain &= world.ModifiedChunks.count({cx, -3, 0}) == 1;
      }
      dirty_service->DrainRebuildQueues(world);
      bool after_drain = true;
      bool cold_matches = true;
      for (int cx = 0; cx < columns; ++cx)
      {
        after_drain &= world.ModifiedChunks.count({cx, -3, 0}) == 1;
        require(world.GetChunkStorage().SaveTerrainColumn(
                    {cx, 0, 0}, world.BlockWorld, folder.string(),
                    *world.BlockRegistry, -33, -48), "fluid fixture cold save");
        UBlockWorld cold;
        cold.SetVerticalProfile(profile);
        cold.SetFluidDefinitions(definitions.get());
        require(world.GetChunkStorage().LoadTerrainColumn(
                    {cx, 0, 0}, cold, folder.string(), *world.BlockRegistry,
                    -33, -48) > 0, "fluid fixture cold load");
        const glm::ivec3 cell(cx * CHUNK_SIZE + 8, -40, 8);
        cold_matches &= cold.GetBlock(cell) == 9 &&
            PackFluidCellState(cold.GetFluidState(cell)) ==
                PackFluidCellState(world.BlockWorld.GetFluidState(cell));
      }
      const bool capacity_exercised = eviction
          ? world.PhysicsTelemetryData.PurgedUpdates > 0
          : world.PhysicsTelemetryData.DroppedUpdates > 0;
      const bool passed = before_drain && after_drain && cold_matches && capacity_exercised;
      report["cases"].push_back({
          {"kind", eviction ? "fluid_visual_eviction" : "fluid_visual_rejection"},
          {"status", passed ? "PASS" : "FAIL"},
          {"dirty_before_drain", before_drain}, {"dirty_after_drain", after_drain},
          {"capacity_exercised", capacity_exercised},
          {"visual_backlog", world.PhysicsTelemetryData.VisualRemeshBacklog},
          {"cold_block_and_fluid_state", cold_matches},
          {"scope", "Physics mutation ownership and direct cold storage; not autosave scheduling or save acknowledgement"}});
      fluid_dirty_retained &= passed;
    }
    require(fluid_dirty_retained, "physics fluid dirty-save lost before visual drain");
    bool fluid_light_delivered = true;
    const bool original_drying_policy = URuntimeTuning::Get().WaterFlowingDries;
    for (int light_case = 0; light_case < 3; ++light_case)
    {
      const bool removal = light_case == 1;
      const bool player_placement = light_case == 2;
      struct RestoreDryingPolicy
      {
        bool Previous{URuntimeTuning::Get().WaterFlowingDries};
        ~RestoreDryingPolicy() { URuntimeTuning::Get().WaterFlowingDries = Previous; }
      } restore_drying_policy;
      // Stable puddles are the default game policy. Exercise the existing
      // opt-in decay path only in this isolated removal fixture.
      if (removal) URuntimeTuning::Get().WaterFlowingDries = true;
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      world.ProceduralTemplate.FillWater = false;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      water.Physics.FluidSpreadPeriodTicks = 1;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      // Fully resident narrow shaft, closed sides: no unknown terrain or
      // alternate sky path. The falling input crosses Y=-32/-33.
      for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
          for (int y = -48; y <= -17; ++y)
            world.BlockWorld.SetBlock({x, y, z}, x == (removal ? 14 : 15) && z == 15 && y > -48 ? BLOCK_AIR : 8);
      const glm::ivec3 cell(15, -33, 15);
      if (removal)
      {
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        // Side alcove with solid floor/roof: no downward spread or incoming
        // fluid. Sky enters horizontally from the adjacent open shaft.
        world.BlockWorld.SetBlock(cell + glm::ivec3(1, 0, 0), BLOCK_AIR);
      }
      else if (!player_placement)
        world.BlockWorld.SetBlock(cell + glm::ivec3(0, 1, 0), 9);
      auto &lighting = world.GetLightingPipeline();
      lighting.RelightAllLoadedChunks(world.BlockWorld, *world.BlockRegistry);
      const auto sky = [&]()
      {
        const auto sample = cell + (removal ? glm::ivec3(1, 0, 0) : glm::ivec3(0, -2, 0));
        return world.BlockWorld.GetChunkManager().GetChunk(
            UChunkManager::WorldToChunk(sample))->GetSkyLightLocal(
                UChunkManager::WorldToLocal(sample));
      };
      const int before = sky();
      PhysicsFeatureFlags flags;
      flags.EnableFluids = true;
      flags.LiquidShadowMode = false;
      world.SetPhysicsProfile(PhysicsProfile::Standard);
      world.SetPhysicsFeatureFlags(flags);
      PhysicsBudgets budgets;
      budgets.BlockEventsPerTickMax = 0;
      budgets.LiquidEventsPerTickMax = 1;
      budgets.VisualRemeshPerTickMax = 0;
      world.SetPhysicsBudgets(budgets);
      world.ModifiedChunks.clear();
      bool accepted = true;
      if (player_placement)
      {
        const auto place_position = BlockCenter(cell);
        require(WorldPosToBlock(place_position) == cell, "player fluid fixture coordinate conversion");
        require(world.BlockRegistry->GetIdByTypeName("water") == 9, "player fluid fixture name resolution");
        require(UFluidSpreadSystem::CanReceiveFluid(world.BlockWorld, *world.BlockRegistry, cell),
                "player fluid fixture must accept water");
        accepted = world.AddObject("water", place_position);
      }
      else
      {
        world.BlockPhysicsService->PublishFluid(cell);
        world.BlockPhysicsService->TickBlockPhysics(world);
      }
      const bool mutated = world.BlockWorld.GetBlock(cell) == (removal ? BLOCK_AIR : 9);
      const int delivered = sky();
      // Delivery oracle only: same solver explicitly invoked after recording
      // the tick's result. Independent solver correctness belongs to S3.
      lighting.RelightAllLoadedChunks(world.BlockWorld, *world.BlockRegistry);
      const int reference = sky();
      const bool changed_reference = removal ? reference > before : reference < before;
      const bool dirty_owner = world.ModifiedChunks.count(UChunkManager::WorldToChunk(cell)) == 1;
      const bool source_state = !player_placement ||
          (world.BlockWorld.GetFluidState(cell).IsSource() &&
           world.BlockWorld.GetFluidState(cell).GetKind() == FluidKind::Water);
      const bool passed = accepted && mutated && dirty_owner && source_state &&
          changed_reference && delivered == reference;
      report["cases"].push_back({
          {"kind", player_placement ? "fluid_light_player_placement" :
                   removal ? "fluid_light_removal_alcove" : "fluid_light_addition"},
          {"status", passed ? "PASS" : "FAIL"}, {"mutated", mutated},
          {"accepted", accepted}, {"dirty_owner", dirty_owner},
          {"source_state", source_state}, {"physics_tick_called", !player_placement},
          {"water_flowing_dries", URuntimeTuning::Get().WaterFlowingDries},
          {"sky_before", before}, {"sky_delivered", delivered},
          {"sky_explicit_recompute", reference},
          {"scope", player_placement
               ? "Actual player AddObject light delivery before any physics tick; not independent solver or framebuffer correctness"
               : "Actual nonemissive fluid tick delivery before maintenance; not independent solver or framebuffer correctness"}});
      fluid_light_delivered &= passed;
    }
    report["fluid_drying_policy_restored"] =
        URuntimeTuning::Get().WaterFlowingDries == original_drying_policy;
    require(report["fluid_drying_policy_restored"].get<bool>(), "fluid probe changed live drying policy");
    require(fluid_light_delivered, "nonemissive fluid mutation did not deliver skylight update");
    for (bool io_failure : {false, true})
    {
      Json fence_evidence = Json::object();
      const auto folder = root / (io_failure ? "write-failure" : "incomplete");
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -64;
      world.ProceduralTemplate.MaxHeight = -17;
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.SetWorldFolderPath(folder.string());
      const glm::ivec3 column(0), floor(0, -4, 0);
      const auto deleted = floor * CHUNK_SIZE + glm::ivec3(1, 2, 3);
      const auto added = deleted + glm::ivec3(1, 0, 0);
      UBlockWorld original;
      UBinaryChunkSerializer codec;
      for (int cy = -4; cy <= -2; ++cy)
      {
        const glm::ivec3 coord(0, cy, 0);
        UChunk frame(coord);
        if (cy != -3) frame.SetBlockLocal({1, 2, 3}, 8);
        const auto decoded = codec.Deserialize(
            codec.Serialize(coord, frame, *world.BlockRegistry).bytes,
            coord, *world.BlockRegistry);
        require(ApplyTerrainSliceLoad(original, coord, &decoded, nullptr,
                                     {column, 1}, 1, false), "original decode");
        if (io_failure || cy == -4)
          require(ApplyTerrainSliceLoad(world.BlockWorld, coord, &decoded, nullptr,
                                       {column, 1}, 1, false), "resident decode");
      }
      auto &storage = world.GetChunkStorage();
      require(storage.SaveTerrainColumn(column, original, folder.string(),
                                       *world.BlockRegistry, -17, -64), "fixture save");
      world.BlockWorld.SetBlock(deleted, BLOCK_AIR);
      world.BlockWorld.SetBlock(added, 8);
      world.ModifiedChunks.insert(floor);
      const auto dirty = world.ModifiedChunks;
      const auto incarnation = world.BlockWorld.GetChunkManager().GetChunk(floor)->GetIncarnation();
      std::ofstream(folder / "users.json") << "{\"fixture\":true}\n";
      std::ofstream(folder / "world_data.json") << "{\"fixture\":true}\n";
      const fs::path light = UChunkStorageService::ColumnLightFilePath(folder.string(), column);
      const fs::path blocker = light.string() + ".tmp";
      if (io_failure)
      {
        require(fs::create_directory(blocker), "directory blocks temp-file open");
        std::ofstream(blocker / "fixture-blocker") << "keep this file";
      }
      // Settle fixture index/signature work before the no-write comparison.
      // The save fence is allowed to finish earlier IO; that must not be
      // confused with new writes issued by an incomplete save attempt.
      const auto prepared_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!world.Persistence->TickDrainAsyncChunkIo(world, 8))
      {
        require(std::chrono::steady_clock::now() < prepared_by, "fixture IO preparation timeout");
        std::this_thread::yield();
      }
      const auto before = capture(folder);
      // Redirect only the main-thread facade call to this owned fixture.
      // Restore the live Core even if an assertion or save throws; never
      // construct another Core whose destructor clears global render caches.
      const auto with_fixture_core = [&](auto action)
      {
        const auto previous_world = core.WorldInstance;
        const auto previous_folder = core.ActiveWorldFolder;
        const auto previous_merge = core.BlockMergeRegistryInstance;
        const auto restore = [&]()
        {
          core.WorldInstance = previous_world;
          core.ActiveWorldFolder = previous_folder;
          core.BlockMergeRegistryInstance = previous_merge;
        };
        core.WorldInstance = std::shared_ptr<UWorld>(&world, [](UWorld *) {});
        core.ActiveWorldFolder = folder;
        core.BlockMergeRegistryInstance.reset();
        try
        {
          action();
        }
        catch (...)
        {
          restore();
          throw;
        }
        restore();
      };
      require(world.IsStreamingEnabled() &&
                  UWorld::HasPersistedTerrainOnDisk(folder.string()),
              "facade fixture selects streaming persisted save path");
      bool facade_failed = false;
      with_fixture_core(
          [&]()
          {
            try
            {
              core.SaveWorld("isolated-save-probe");
            }
            catch (const std::exception &error)
            {
              facade_failed = std::string(error.what())
                                  .find(io_failure ? "Terrain save failed"
                                                   : "Terrain is incomplete") !=
                              std::string::npos;
            }
          });
      require(facade_failed, "full Core SaveWorld must reject unsaved terrain "
                             "instead of metadata-only success");
      require(capture(folder) == before && world.ModifiedChunks == dirty,
              "failed full Core SaveWorld preserves files and dirty edits");
      Json runner_failures = Json::array();
      for (auto op : {WorldRunnerOp::SaveThenLoad,
                      WorldRunnerOp::SaveThenCreate, WorldRunnerOp::Shutdown})
      {
        Progress runner_progress;
        with_fixture_core(
            [&]()
            {
              UWorldOperationRunner runner(core, world);
              WorldRunnerRequest request;
              request.op = op;
              request.worldName = "must-not-replace-current-world";
              request.saveConfigAfter = false;
              request.shutdownSaveSession = true;
              request.shutdownCloseApplication = true;
              runner.Start(request);
              const auto runner_by =
                  std::chrono::steady_clock::now() + std::chrono::seconds(12);
              while (!runner.Tick(runner_progress, 1))
              {
                require(std::chrono::steady_clock::now() < runner_by,
                        "failed save runner bound");
                require(
                    std::find(runner_progress.phases.begin(),
                              runner_progress.phases.end(),
                              "teardown") == runner_progress.phases.end(),
                    "save failure must stop runner before replacing terrain");
                std::this_thread::yield();
              }
              require(!runner.Succeeded() && runner.HasSaveFailure() &&
                          !runner.ShouldCloseApplication() &&
                          !runner.ErrorMessage().empty() &&
                          core.GetActiveWorldFolder() == folder &&
                          capture(folder) == before &&
                          world.ModifiedChunks == dirty &&
                          world.BlockWorld.GetBlock(deleted) == BLOCK_AIR &&
                          world.BlockWorld.GetBlock(added) == 8 &&
                          world.BlockWorld.GetChunkManager()
                                  .GetChunk(floor)
                                  ->GetIncarnation() == incarnation,
                      "failed save runner preserves current world and denies "
                      "close");
            });
        runner_failures.push_back(
            {{"operation", op == WorldRunnerOp::Shutdown ? "shutdown"
                           : op == WorldRunnerOp::SaveThenLoad
                               ? "save_then_load"
                               : "save_then_create"},
             {"phases", runner_progress.phases},
             {"status", "PASS"}});
        world.ResumeAfterSessionSave();
      }
      Progress failed;
      world.BeginCooperativeSave(folder.string(), false);
      size_t ticks = 0;
      const auto failed_by = std::chrono::steady_clock::now() + std::chrono::seconds(12);
      while (!world.TickCooperativeSave(failed, 1))
      {
        ++ticks;
        require(std::chrono::steady_clock::now() < failed_by, "save failure must become terminal");
        std::this_thread::yield();
      }
      const auto error = world.GetCooperativeSaveError();
      require(!error.empty() && !world.HasActiveCooperativeOperation(), "failure result");
      require(error.find(io_failure ? "Terrain save failed" : "Terrain is incomplete") != std::string::npos,
              "correct incomplete or filesystem failure branch");
      require(before == capture(folder), "failed save changed files");
      require(world.ModifiedChunks == dirty, "failed save cleared dirty state");
      require(world.BlockWorld.GetBlock(deleted) == BLOCK_AIR &&
              world.BlockWorld.GetBlock(added) == 8 &&
              world.BlockWorld.GetChunkManager().GetChunk(floor)->GetIncarnation() == incarnation,
              "failed save lost RAM edit or owner");
      for (const auto &phase : failed.phases)
        require(phase != "done" && phase != "save_metadata", "failure claimed completion");
      world.SaveSessionSnapshotMetadataOnly(folder.string());
      require(world.ModifiedChunks == dirty,
              "metadata-only refresh must not acknowledge terrain edits");
      if (io_failure)
      {
        require(fs::remove(blocker / "fixture-blocker"), "remove only fixture blocker file");
        require(fs::remove(blocker), "remove only empty fixture blocker directory");
      }
      require(storage.LoadTerrainColumn(column, world.BlockWorld, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0, "recover disk base");
      Progress recovered;
      world.BeginCooperativeSave(folder.string(), false);
      ticks = 0;
      const auto recovered_by = std::chrono::steady_clock::now() + std::chrono::seconds(12);
      while (!world.TickCooperativeSave(recovered, 1))
      {
        ++ticks;
        require(std::chrono::steady_clock::now() < recovered_by, "recovered save must finish");
        std::this_thread::yield();
      }
      require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty(),
              "retry did not reset error or finish saving");
      UBlockWorld cold;
      require(storage.LoadTerrainColumn(column, cold, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0 &&
              cold.GetBlock(deleted) == BLOCK_AIR && cold.GetBlock(added) == 8,
              "successful retry lost persisted edit");
      const auto facade_cell = deleted + glm::ivec3(4, 0, 0);
      world.BlockWorld.SetBlock(facade_cell, 8);
      world.ModifiedChunks.insert(floor);
      with_fixture_core([&]() { core.SaveWorld("isolated-save-probe"); });
      UBlockWorld facade_cold;
      require(
          world.ModifiedChunks.empty() &&
              storage.LoadTerrainColumn(column, facade_cold, folder.string(),
                                        *world.BlockRegistry, -17, -64) > 0 &&
              facade_cold.GetBlock(facade_cell) == 8,
          "full Core SaveWorld persists edits in an existing streaming world");
      const auto runner_cell = deleted + glm::ivec3(5, 0, 0);
      world.BlockWorld.SetBlock(runner_cell, 8);
      world.ModifiedChunks.insert(floor);
      Progress runner_retry;
      with_fixture_core(
          [&]()
          {
            UWorldOperationRunner runner(core, world);
            WorldRunnerRequest request;
            request.op = WorldRunnerOp::Save;
            request.saveConfigAfter = false;
            runner.Start(request);
            const auto runner_by =
                std::chrono::steady_clock::now() + std::chrono::seconds(12);
            while (!runner.Tick(runner_retry, 1))
            {
              require(std::chrono::steady_clock::now() < runner_by,
                      "successful save runner bound");
              std::this_thread::yield();
            }
            require(runner.Succeeded() && !runner.HasSaveFailure() &&
                        !runner.ShouldCloseApplication(),
                    "runner retry reports successful save");
          });
      UBlockWorld runner_cold;
      require(
          world.ModifiedChunks.empty() &&
              storage.LoadTerrainColumn(column, runner_cold, folder.string(),
                                        *world.BlockRegistry, -17, -64) > 0 &&
              runner_cold.GetBlock(runner_cell) == 8,
          "runner retry persists resident edit");
      // The unload path must wait for real ACKs and reject an ACK for a
      // snapshot which changed while the worker owned its immutable bytes.
      world.ProceduralTemplate.AsyncChunkIo = true;
      world.Persistence->EnsureChunkIoInitialized();
      const auto drain = [&]()
      {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (storage.IsColumnSavePending(column))
        {
          require(std::chrono::steady_clock::now() < deadline, "async save ACK timeout");
          world.Persistence->TickAsyncChunkIo(world, 32, 6.0);
          std::this_thread::yield();
        }
      };
      const auto during = deleted + glm::ivec3(2, 0, 0);
      world.ModifiedChunks.insert(floor);
      require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                  storage.IsColumnSavePending(column), "enqueue cannot admit retirement");
      world.BlockWorld.SetBlock(during, 8);
      drain();
      require(
          !world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
              storage.IsColumnSavePending(column) &&
              world.ModifiedChunks.count(floor),
          "new local edit must invalidate old ACK without clearing dirty "
          "state");
      drain();
      require(world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                  world.BlockWorld.GetChunkManager().HasChunk(floor) &&
                  !world.ModifiedChunks.count(floor),
              "only the current completed save admits retirement");
      if (io_failure)
      {
        require(fs::create_directory(blocker), "async temp blocker is owned");
        std::ofstream(blocker / "fixture-blocker") << "keep this file";
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column), "failed async save starts with RAM retained");
        drain();
        require(
            !world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                !storage.IsColumnSavePending(column) &&
                world.BlockWorld.GetBlock(during) == BLOCK_AIR &&
                world.ModifiedChunks.count(floor) &&
                world.BlockWorld.GetChunkManager().HasChunk(floor),
            "failed completed batch cannot authorize retirement");
        require(fs::remove(blocker / "fixture-blocker") && fs::remove(blocker),
                "remove only the owned async blocker");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TrySaveTerrainColumnForUnload(world, column))
        {
          require(std::chrono::steady_clock::now() < deadline, "async recovery timeout");
          world.Persistence->TickAsyncChunkIo(world, 32, 6.0);
          std::this_thread::yield();
        }
      }
      UBlockWorld async_cold;
      require(storage.LoadTerrainColumn(column, async_cold, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0 &&
                  async_cold.GetBlock(during) == (io_failure ? BLOCK_AIR : 8),
              "actual async recovery persisted the newest edit");
      if (io_failure)
      {
        // A late write to the old folder must update its physical index but
        // cannot acknowledge a save or grant retirement in the new folder.
        world.ProceduralTemplate.MaxHeight = 15;
        for (int cy = -1; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(0, cy, 0);
          UChunk frame(extra);
          if (cy == 0) frame.SetBlockLocal({1, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes,
              extra, *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded, nullptr,
                                       {column, 1}, 1, false), "expanded save fixture");
        }
        require(storage.GetHighestChunkSliceOnDisk(folder.string(), column) == -2,
                "old folder index starts below the new queued ceiling");
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column), "old-folder save is pending");
        const auto switched = root / "switched-folder";
        world.SetWorldFolderPath(switched.string());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TickDrainAsyncChunkIo(world, 32))
        {
          require(std::chrono::steady_clock::now() < deadline, "old-folder ACK drain timeout");
          std::this_thread::yield();
        }
        require(storage.GetHighestChunkSliceOnDisk(folder.string(), column) == 0,
                "late successful old-folder write updates its own index");
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column),
                "old-folder ACK cannot grant new-folder retirement");
        drain();
        require(world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "new folder requires its own complete save");
        // Hold an older immutable terrain write while requesting a newer
        // cooperative save to the same folder. No synchronous phase may pass
        // this active write. RAII releases the worker on every assertion exit.
        auto gate = std::make_shared<AsyncSaveWriteProbeGate>();
        struct ReleaseGate
        {
          std::shared_ptr<AsyncSaveWriteProbeGate> value;
          ~ReleaseGate() { value->released.store(true, std::memory_order_release); }
        } release{gate};
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate = gate;
        world.BlockWorld.SetBlock(during, 8);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "old snapshot is queued before the newer edit");
        const auto entered_by = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!gate->entered.load(std::memory_order_acquire))
        {
          require(std::chrono::steady_clock::now() < entered_by, "writer did not enter probe gate");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        bool snapshot_refused = false;
        try { world.SaveSessionSnapshot(switched.string(), true); }
        catch (const std::runtime_error &error)
        {
          snapshot_refused = std::string(error.what()).find("Pending terrain I/O") != std::string::npos;
        }
        require(snapshot_refused && world.ModifiedChunks.count(floor) == 1,
                "full snapshot cannot bypass a held older writer");
        Progress fenced;
        world.BeginCooperativeSave(switched.string(), false);
        const bool terminal = world.TickCooperativeSave(fenced, 1);
        require(!terminal && !fenced.phases.empty() &&
                    fenced.phases.back() == "save_waiting_for_io" &&
                    world.ModifiedChunks.count(floor) == 1,
                "cooperative save must wait for active writes");
        gate->released.store(true, std::memory_order_release);
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto saved_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(fenced, 1))
        {
          require(std::chrono::steady_clock::now() < saved_by, "fenced save did not finish");
          std::this_thread::yield();
        }
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    world.Persistence->IsAsyncChunkIoQuiescent(),
                "new cooperative save must finish after the old batch");
        UBlockWorld final_cold;
        require(storage.LoadTerrainColumn(column, final_cold, switched.string(),
                                         *world.BlockRegistry, 15, -64) > 0 &&
                    final_cold.GetBlock(during) == BLOCK_AIR,
                "old writer cannot overwrite the newer cooperative edit");
        auto slow_gate = std::make_shared<AsyncSaveWriteProbeGate>();
        slow_gate->limit = std::chrono::seconds(20);
        ReleaseGate release_slow{slow_gate};
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate = slow_gate;
        world.BlockWorld.SetBlock(during, 8);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "timeout probe queues its own older snapshot");
        const auto slow_entered_by = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!slow_gate->entered.load(std::memory_order_acquire))
        {
          require(std::chrono::steady_clock::now() < slow_entered_by, "slow gate not entered");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        Progress timeout_progress;
        world.BeginCooperativeSave(switched.string(), false);
        const auto timed_at = std::chrono::steady_clock::now();
        while (!world.TickCooperativeSave(timeout_progress, 1))
        {
          require(std::chrono::steady_clock::now() - timed_at < std::chrono::seconds(13),
                  "save I/O wait must have a bounded error result");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double waited_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - timed_at).count();
        require(waited_seconds >= 9.99 && waited_seconds < 13.0,
                "native timeout must respect the actual ten-second wait bound");
        fence_evidence["timeout_seconds"] = waited_seconds;
        fence_evidence["timeout_phases"] = timeout_progress.phases;
        fence_evidence["fenced_save_phases"] = fenced.phases;
        require(!world.GetCooperativeSaveError().empty() &&
                    world.GetCooperativeSaveError().find("World files are still busy") != std::string::npos &&
                    world.ModifiedChunks.count(floor) == 1 &&
                    !world.Persistence->IsAsyncChunkIoQuiescent(),
                "I/O timeout cannot clear edits or pretend the writer stopped");
        for (const auto &phase : timeout_progress.phases)
          require(phase != "done" && phase != "save_metadata" && phase != "scan",
                  "I/O timeout cannot enter a synchronous save phase");
        slow_gate->released.store(true, std::memory_order_release);
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto drain_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TickDrainAsyncChunkIo(world, 8))
        {
          require(std::chrono::steady_clock::now() < drain_by, "timeout recovery drain");
          std::this_thread::yield();
        }
        Progress retried;
        world.BeginCooperativeSave(switched.string(), false);
        const auto retry_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(retried, 1))
        {
          require(std::chrono::steady_clock::now() < retry_by, "timeout recovery save");
          std::this_thread::yield();
        }
        UBlockWorld retry_cold;
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    world.Persistence->IsAsyncChunkIoQuiescent() &&
                    storage.LoadTerrainColumn(column, retry_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    retry_cold.GetBlock(during) == BLOCK_AIR,
                "retry after I/O timeout must persist the newer edit");
        Progress late;
        world.BeginCooperativeSave(switched.string(), false);
        const auto chunks_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::find(late.phases.begin(), late.phases.end(), "chunks") == late.phases.end())
        {
          require(std::chrono::steady_clock::now() < chunks_by &&
                      !world.TickCooperativeSave(late, 1), "late-edit save reaches terrain phase");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, 8);
        world.ModifiedChunks.insert(floor);
        const glm::ivec3 late_column(1, 0, 0), late_floor(1, -4, 0);
        for (int cy = -4; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(1, cy, 0);
          UChunk frame(extra);
          if (cy == -4) frame.SetBlockLocal({3, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes,
              extra, *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded, nullptr,
                                       {late_column, 1}, 1, false), "late new-column fixture");
        }
        const auto late_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(late, 1))
        {
          require(std::chrono::steady_clock::now() < late_by, "late-edit completion bound");
          std::this_thread::yield();
        }
        require(world.ModifiedChunks.count(floor) && world.ModifiedChunks.count(late_floor),
                "save cannot acknowledge post-write edits or unlisted new columns");
        require(!world.GetCooperativeSaveError().empty(),
                "exit save must refuse to discard newer unsaved data");
        fence_evidence["late_exit_phases"] = late.phases;
        Progress latest;
        world.BeginCooperativeSave(switched.string(), false);
        const auto latest_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(latest, 1))
        {
          require(std::chrono::steady_clock::now() < latest_by, "late-edit retry bound");
          std::this_thread::yield();
        }
        UBlockWorld late_cold;
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    storage.LoadTerrainColumn(column, late_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    storage.LoadTerrainColumn(late_column, late_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    late_cold.GetBlock(during) == 8 &&
                    late_cold.GetBlock({19, -62, 3}) == 8,
                "retry persists both newer edit and unlisted column");
        // A resumable save may finish its cohort, but newer changes must be
        // retained for the following save rather than silently acknowledged.
        Progress background;
        world.BeginCooperativeSave(switched.string(), true);
        const auto background_by =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::find(background.phases.begin(), background.phases.end(),
                         "save_metadata") == background.phases.end())
        {
          require(std::chrono::steady_clock::now() < background_by &&
                      !world.TickCooperativeSave(background, 1),
                  "resumable save reaches metadata");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        const glm::ivec3 background_column(2, 0, 0), background_floor(2, -4, 0);
        for (int cy = -4; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(2, cy, 0);
          UChunk frame(extra);
          if (cy == -4)
            frame.SetBlockLocal({3, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes, extra,
              *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded,
                                        nullptr, {background_column, 1}, 1,
                                        false),
                  "background new-column fixture");
        }
        while (!world.TickCooperativeSave(background, 1))
        {
          require(std::chrono::steady_clock::now() < background_by,
                  "resumable save completion bound");
          std::this_thread::yield();
        }
        UBlockWorld background_cold;
        require(
            world.GetCooperativeSaveError().empty() &&
                world.ModifiedChunks.count(floor) &&
                world.ModifiedChunks.count(background_floor) &&
                storage.LoadTerrainColumn(column, background_cold,
                                          switched.string(),
                                          *world.BlockRegistry, 15, -64) > 0 &&
                background_cold.GetBlock(during) == 8 &&
                !fs::exists(storage.ChunkFilePath(switched.string(),
                                                  background_floor,
                                                  ChunkDiskFormat::Binary)),
            "resumable save retains newer edits outside its persisted cohort");
        Progress background_retry;
        world.BeginCooperativeSave(switched.string(), true);
        const auto background_retry_by =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(background_retry, 1))
        {
          require(std::chrono::steady_clock::now() < background_retry_by,
                  "resumable retry bound");
          std::this_thread::yield();
        }
        UBlockWorld background_retry_cold;
        require(world.GetCooperativeSaveError().empty() &&
                    world.ModifiedChunks.empty() &&
                    storage.LoadTerrainColumn(
                        column, background_retry_cold, switched.string(),
                        *world.BlockRegistry, 15, -64) > 0 &&
                    storage.LoadTerrainColumn(
                        background_column, background_retry_cold,
                        switched.string(), *world.BlockRegistry, 15, -64) > 0 &&
                    background_retry_cold.GetBlock(during) == BLOCK_AIR &&
                    background_retry_cold.GetBlock({35, -62, 3}) == 8,
                "resumable retry persists deletion and new column");
        fence_evidence["resumable_late_edit_phases"] = background.phases;
      }
      report["cases"].push_back(
          {{"kind", io_failure ? "write_failure" : "incomplete"},
           {"status", "PASS"},
           {"failure", error},
           {"failure_phases", failed.phases},
           {"recovery_phases", recovered.phases},
           {"files_preserved", before.size()},
           {"dirty_preserved", dirty.size()},
           {"cold_edit_preserved", true},
           {"unload_waited_for_ack", true},
           {"newer_edit_invalidated_ack", true},
           {"async_failure_retained", io_failure},
           {"async_cold_edit_preserved", true},
           {"folder_switch_checked", io_failure},
           {"cooperative_write_fence_checked", io_failure},
           {"full_core_save_failure_and_retry_checked", true},
           {"runner_failure_gates", runner_failures},
           {"runner_retry_checked", true},
           {"metadata_only_preserved_dirty", true},
           {"snapshot_fence_checked", io_failure},
           {"io_timeout_and_retry_checked", io_failure},
           {"write_fence_evidence", fence_evidence},
           {"late_edit_and_column_checked", io_failure}});
    }
    report["status"] = "PASS";
  }
  catch (const std::exception &error) { report["error"] = error.what(); }
  std::ofstream(root / "report.json") << report.dump(2) << '\n';
}
} // namespace cutum
