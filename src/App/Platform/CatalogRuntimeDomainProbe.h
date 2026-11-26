#pragma once
#include "App/Core.h"
#include "App/Application.h"
#include "App/Platform/WindowsGpuMemoryReport.h"
#include "Render/Engine/GeometryEngine.h"
#include "Gui/Core/GuiIconSource.h"
#include "Gui/Cache/CreatureItemIconStorage.h"
#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Preview/ItemLabSceneRenderer.h"
#include "Gui/Screens/ItemLabScreen.h"
#include "Items/FpViewmodelRenderer.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/Lighting/AsyncRelightBuilder.h"
#include "WorldGen/Core/CatalogRuntimeDomain.h"
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "WorldGen/Core/AllocatorOwnerCensus.h"
#include "WorldGen/Core/PipelineAllocatorThreadView.h"
#include "WorldGen/Core/RuntimeNameStorage.h"
#include "Core/Memory/EngineThreadStorage.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Core/Memory/VisualFilesystemSource.h"
#include "Core/Memory/VisualLoaderSource.h"
#include "Core/Memory/WindowsCrtFileTables.h"
#include "Core/Memory/WindowsCrtLocaleStorage.h"
#include "Core/Memory/WindowsCrtThreadStorage.h"
#include "Core/Memory/WindowsExecutableTlsStorage.h"
#include "Core/Memory/WindowsProcessMemory.h"
#include "Core/Memory/NativeThreadStack.h"
#include "Items/ItemSceneCursor.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Diagnostics/CreatureQaFootProbe.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemRenderAsset.h"
#include "Render/Textures/VisualTextureUpload.h"
#include "Test/GuiContextStorageFixture.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

namespace cutum
{
struct CatalogApplicationDomain final : CatalogRuntimeDomain
{
  AllocatorOwnerCensus AllocationOwners;
  const UCore *owner_core{};const UApplication *owner_application{};
  std::vector<WorldOwnedStorageSnapshot> world_owned;
  std::vector<const UWorld*> member_worlds;
  std::vector<const UGeometryEngine*> member_geometry;
  using CatalogRuntimeDomain::Geometry;
  void Core(const UCore *v){if(Enter(Kind::Core,v))v->InspectCatalogDomain(*this);}
  void Application(const UApplication *v){if(Enter(Kind::Application,v))v->InspectCatalogDomain(*this);}
  void World(const UWorld *v){if(Enter(Kind::World,v)){member_worlds.push_back(v);AllocationOwners.Manager(v->GetBlockWorld().GetChunkManager(),nullptr,false,!ActiveScopedSharedResource);v->InspectCatalogDomain(*this);v->GetBlockWorld().InspectCatalogRoots(*this);world_owned.push_back(v->InspectOwnedGeneratorsAndCallbacks(owner_core,owner_application,true));}}
  void Geometry(const UGeometryEngine *v){if(Enter(Kind::Geometry,v)){member_geometry.push_back(v);v->InspectCatalogDomain(*this);v->InspectCatalogRoots(*this);}}
  void GuiIcon(const UGuiIconSource *v){if(Enter(Kind::GuiIcon,v))v->InspectCatalogDomain(*this);}
  void ObjectIcon(const UObjectIconCache *v){if(Enter(Kind::ObjectIcon,v))v->InspectCatalogDomain(*this);}
  void Preview(const UContentPreviewRenderer *v){if(Enter(Kind::Preview,v))v->InspectCatalogDomain(*this);}
  void Viewmodel(const UFpViewmodelRenderer *v){if(Enter(Kind::Viewmodel,v))v->InspectCatalogDomain(*this);}
  void ItemLab(const UItemLabScreen *v){if(Enter(Kind::ItemLab,v))v->InspectCatalogDomain(*this);}
  void ItemLabScene(const UItemLabSceneRenderer *v){if(Enter(Kind::ItemLabScene,v))v->InspectCatalogDomain(*this);}
  void MeshService(const UWorldMeshService *v){if(Enter(Kind::MeshService,v))MeshCache(&v->GetCache());}
  void MeshCache(const UChunkMeshCache *v){if(Enter(Kind::MeshCache,v)){v->InspectJobPools(*this);v->InspectCatalogRoots(*this);v->InspectCompletedCatalogRoots(*this);}}
  void Streaming(const UWorldStreaming *v){if(Enter(Kind::Streaming,v))if(const auto *scheduler=v->GetChunkScheduler()){scheduler->InspectJobPools(*this);AllocationOwners.Scheduler(*scheduler);}}
  void Persistence(const UWorldPersistence *v){if(Enter(Kind::Persistence,v))v->InspectCatalogDomain(*this);}
  void Io(const UAsyncChunkIO *v){if(Enter(Kind::Io,v))v->InspectJobPools(*this);}
  void Relight(const UAsyncRelightBuilder *v){if(v){v->InspectJobPools(*this);v->InspectCompletedCatalogRoots(*this);}}
  void Coop(const UWorldCooperativeSession *v){if(Enter(Kind::Coop,v))v->InspectCatalogDomain(*this);}
};
struct CatalogLauncherRootsView
{
  const std::shared_ptr<UBlockDefinitionStorage> &blocks;
  const std::shared_ptr<UTextureBaseStorage> &bases;
  const std::shared_ptr<UTextureCubeStorage> &cubes;
  const std::shared_ptr<UObjectLibrary> &objects;
  const std::shared_ptr<UWorld> &world;
  const std::shared_ptr<UGeometryEngine> &geometry;
  const std::shared_ptr<UCore> *core{};
  const std::shared_ptr<UApplication> *application{};
};
inline nlohmann::json DescribeNativeThreadStacks()
{
  const auto stacks = InspectNativeThreadStacks();
  auto owners = nlohmann::json::object();
  size_t sampled = 0, reserved = 0, committed = 0;
  for (size_t i = 0; i < stacks.rows.size(); ++i)
  {
    const auto &row = stacks.rows[i];
    sampled += row.sampled;
    reserved += row.reserved;
    committed += row.committed;
    owners[NativeThreadNames[i]] = {
        {"entered", row.entered}, {"retired", row.retired}, {"borrowed", row.borrowed},
        {"active", row.active}, {"peak_active", row.peakActive}, {"sampled", row.sampled},
        {"reserved_bytes", row.reserved}, {"peak_reserved_bytes", row.peakReserved},
        {"committed_bytes", row.committed}, {"guard_bytes", row.guard}};
  }
  const auto mainScopes = stacks.rows[static_cast<size_t>(NativeThreadKind::Main)].active;
  const auto crt = BoundWindowsCrtThreadStorage(stacks.registered, mainScopes);
  const auto tls = ObserveWindowsExecutableTls(stacks, crt);
  const auto tlsStatus = tls.status == ExecutableTlsStatus::Observed ? "PASS" :
      tls.status == ExecutableTlsStatus::CohortChanged ? "COHORT_CHANGED" :
      tls.status == ExecutableTlsStatus::Unobserved ? "UNOBSERVED" : "UNSUPPORTED";
  const auto locale = ObserveWindowsCrtLocale();
  return {
      {"schema", "native-thread-stacks.v1"},
      {"status", stacks.processThreadsObserved && stacks.registered && sampled == stacks.registered ? "PASS" : "UNSUPPORTED"},
      {"process_threads_observed", stacks.processThreadsObserved}, {"process_threads", stacks.processThreads},
      {"registered_threads", stacks.registered}, {"matched_threads", stacks.matched},
      {"unregistered_process_threads", stacks.processThreads - stacks.matched},
      {"sampled_stacks", sampled}, {"reserved_bytes", reserved}, {"committed_bytes", committed},
      {"owners", std::move(owners)}, {"scope_inline_bytes", sizeof(NativeThreadStackOwner)},
      {"row_counters_inline_bytes", sizeof(stacks.rows)},
      {"linked_crt_thread_storage", {
          {"schema", "windows-crt-thread-storage.v1"}, {"status", crt.supported ? "PASS" : "UNSUPPORTED"},
          {"primary_scopes", crt.primaryScopes}, {"worker_scopes", crt.workerScopes},
          {"ucrt_ptd_requested_upper", crt.ucrtPtdUpper}, {"vcrt_worker_ptd_requested_upper", crt.workerVcrtPtdUpper},
          {"launch_parameter_requested_upper", crt.launchParameterUpper},
          {"time_scratch_requested_upper", crt.timeScratchUpper}, {"main_vcrt_static_bytes", crt.mainVcrtStaticBytes},
          {"exe_tls_raw_per_thread", crt.tlsRawPerThread}, {"exe_tls_zero_fill_per_thread", crt.tlsZeroFillPerThread},
          {"exe_tls_template_payload", crt.tlsTemplatePayload}, {"whole_runtime_complete", false}}},
      {"windows_executable_tls_heap", {
          {"schema", "windows-executable-tls-heap.v1"}, {"status", tlsStatus},
          {"registered_threads", stacks.registered}, {"sampled_threads", tls.threads},
          {"block_data_bytes_including_template", tls.dataBytes}, {"heap_entry_overhead_bytes", tls.overheadBytes},
          {"template_payload_bytes", tls.templateBytes}, {"above_template_data_bytes", tls.dataBytes - tls.templateBytes},
          {"largest_block_data_bytes", tls.largestBlockBytes}, {"whole_runtime_complete", false}}},
      {"crt_locale_storage", {
          {"schema", "windows-crt-locale-storage.v1"},
          {"status", locale.sampled ? "PASS" : locale.supported ? "UNOBSERVED" : "UNSUPPORTED"},
          {"initial_named_bytes_per_image", locale.initialNamedBytes},
          {"snapshot_requested_bytes", locale.snapshotRequestBytes},
          {"calling_locale_root_requested_bytes", locale.localeRootRequest},
          {"calling_multibyte_root_requested_bytes", locale.multibyteRootRequest},
          {"calling_thread_initial_locale", locale.initialLocale},
          {"calling_thread_initial_multibyte", locale.initialMultibyte},
          {"calling_thread_private_mode", locale.privateMode}, {"whole_runtime_complete", false}}},
      {"whole_runtime_complete", false}};
}

inline void WriteCatalogRuntimeDomain(const UCore &core,const UApplication &application,
    CatalogLauncherRootsView launcher,const char *entrypoint,const char *phase)
{
  const char *directory=std::getenv("CUBA_CATALOG_RUNTIME_DOMAIN");if(!directory || !*directory)return;
  using Json=nlohmann::json;CatalogApplicationDomain out;out.owner_core=&core;out.owner_application=&application;
  out.Core(&core);out.Application(&application);
  out.World(launcher.world.get());out.Geometry(launcher.geometry.get());
  out.BlockStorage(launcher.blocks,"launcher");out.TextureBases(launcher.bases);out.TextureCubes(launcher.cubes);out.Library(launcher.objects);
  const auto content=CaptureWorldGenContentSnapshot();out.Content(content);out.CallingThread();out.ActiveDirectory();
  InspectCallingThreadRenderWitnessRoots(out);
  const auto engine_storage=InspectCallingThreadEngineStorage(true,true);
  const auto pipeline_allocator_view=InspectPipelineCallingThreadAllocators(out.AllocationOwners);
  const auto allocation_owners=out.AllocationOwners.Result(true);
  const auto census=out.Catalogs();Json pools=Json::array(),roles=Json::array();
  const auto retained=BoundRetainedCatalogRequests(census);
  bool active_roles_current=true;
  for(const auto &role:out.block_roles){
    const bool current=role.storage==core.GetBlockDefinitionStorage().get();
    roles.push_back({{"role",role.role},{"current",current}});
    if(role.role!="launcher" && !current)active_roles_current=false;
  }
  for(const auto &pool:out.pools)pools.push_back({{"kind",pool.kind},{"workers",pool.state.workers},{"pending",pool.state.pending},{"active",pool.state.active},{"source_worker_cap",pool.worker_cap}});
  const auto thread_upper=out.CallingAndPoolThreadUpper();
  Json report={{"schema","catalog-runtime-domain.v1"},{"status",out.Supported()?"PASS":"UNSUPPORTED"},{"entrypoint",entrypoint},{"phase",phase},
    {"worlds",out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)]},{"cores",out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Core)]},
    {"applications",out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Application)]},{"block_storages",census.Objects(CatalogObjectKind::BlockStorage)},
    {"block_catalogs",census.Objects(CatalogObjectKind::Blocks)},{"libraries",census.Objects(CatalogObjectKind::PrefabIndex)},
    {"known_catalog_bytes",census.known_storage_bytes},{"block_roles",roles},{"active_roles_current",active_roles_current},
    {"launcher_distinct_from_core",launcher.blocks.get()!=core.GetBlockDefinitionStorage().get()},
    {"pool_count",out.pools.size()},{"pool_workers",out.workers},{"calling_and_pool_thread_upper",thread_upper},{"pools",pools},
    {"unknown_owners",out.unknown_owners},{"calling_thread_convenience_inline_bytes",census.calling_thread_inline_bytes},
    {"retained_requested_upper",retained.requested_heap_upper},{"retained_upper_supported",retained.supported && !retained.overflow},
    {"publication_catalog_handles",out.Counts.catalog_handles},{"publication_fields",out.Counts.Nodes(CatalogPublicationRootInspector::Node::Field)},
    {"publication_mesh_results",out.Counts.Nodes(CatalogPublicationRootInspector::Node::Mesh)},
    {"publication_bindings",out.Counts.Nodes(CatalogPublicationRootInspector::Node::Binding)},
    {"scope","One identity-deduplicated supplied Core/Application/launcher/UI/world resource+field/cache/completed/backend/save graph and actual member pools. Own calling-thread reads only;foreign TLS/queued callables/source histories/caller escapes/constructing components must be supplied separately. Requested-data upper,not process/CRT/stack/GPU acceptance."}};
  report["allocator_owner_domain"]={{"schema","allocator-owner-selected-runtime.v1"},
    {"status",allocation_owners.supported?"PASS":"UNSUPPORTED"},{"blocks",allocation_owners.blocks},
    {"covered",allocation_owners.covered},{"weak_only",allocation_owners.weak_only},
    {"additional_requests",allocation_owners.additional_requests},{"covered_requests",allocation_owners.covered_requests},
    {"unknown",allocation_owners.unknown},{"aliases",allocation_owners.aliases},{"overflow",allocation_owners.overflow},
    {"calling_pipeline_initialized",pipeline_allocator_view.initialized},{"calling_pipeline_builds",pipeline_allocator_view.builds},
    {"whole_runtime_complete",false},
    {"scope","Actual supplied E11 worlds/managers/free-list/schedulers/completed-buffer and direct calling-thread allocator slots;ordinary main-world field source profile. Active worker/caller/TLS scratch/weak-only external source views and total process coverage remain separate. Inspector owns no strong/weak source mirrors."}};
  const auto names=InspectCallingThreadRuntimeNames();
  report["runtime_name_storage"]={{"schema","runtime-name-calling-thread.v1"},{"status",names.supported?"PASS":"UNSUPPORTED"},
    {"flags",names.flags},{"flag_capacity",names.flag_capacity},{"vector_requests",names.environment_vector},
    {"name_requests",names.environment_names},{"merge_requests",names.merge_name},{"requested",names.Total()},
    {"inline_shells_already_counted",names.layout.environment_inline+names.layout.merge_inline},
    {"whole_runtime_complete",false},{"scope","Read-only actual calling-thread cache capacities;foreign TLS and total process remain separate."}};
  report["engine_thread_storage"]={{"schema","engine-calling-thread-storage.v1"},{"status",engine_storage.supported?"PASS":"UNSUPPORTED"},
    {"inline_bytes",engine_storage.layout.inline_bytes},{"widths",engine_storage.layout.widths},{"requests",engine_storage.requests},
    {"requested",engine_storage.Total()},{"cpu_entries",engine_storage.cpu_entries},{"capture_owners",engine_storage.capture_owners},
    {"binding_handles",engine_storage.binding_handles},{"gpu_capacity",engine_storage.gpu_capacity},{"preview_entries",engine_storage.preview_entries},
    {"preview_buckets",engine_storage.preview_buckets},{"celestial_capacity",engine_storage.celestial_capacity},{"whole_runtime_complete",false},
    {"scope","Actual calling-thread caches and same-graph publication binding roots;foreign TLS/window/CRT/GPU/process remain separate."}};
  std::vector<const WorldOwnedStorageSnapshot*> owned_views;Json owned_rows=Json::array();
  for(const auto &source:out.world_owned){owned_views.push_back(&source);std::array<size_t,10> callbacks{},captures{};std::array<bool,10> present{};
    for(size_t i=0;i<callbacks.size();++i){callbacks[i]=source.callbacks[i].requests;captures[i]=source.callbacks[i].capture_inline;present[i]=source.callbacks[i].present;}
    owned_rows.push_back({{"supported",source.supported},{"generation_present",source.generation.initialized},{"generation_requests",source.generation.requests},
      {"lighting_requests",source.lighting_requests},{"lighting_kind",source.lighting_kind},{"callback_requests",callbacks},{"callback_capture_inline",captures},
      {"callback_present",present},{"borrowed_context_matches",source.borrowed_context_matches},{"requested",source.Total()}});
  }
  const auto owned=JoinWorldOwnedStorage(owned_views,out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)],true,true);
  const auto owned_layout=GetWorldOwnedStorageLayout();
  report["world_owned_storage"]={{"schema","world-owned-runtime.v1"},{"status",owned.supported?"PASS":"UNSUPPORTED"},
    {"worlds",owned.worlds},{"requested",owned.requested},{"rows",owned_rows},{"generation_layout",owned_layout.generation.widths},
    {"lighting_layout",owned_layout.lighting},{"parent_inline",{owned_layout.world_inline,owned_layout.block_world_inline,owned_layout.function_inline}},
    {"parent_shells_counted_here",false},{"whole_runtime_complete",false},
    {"scope","Unique supplied E11 worlds;selected private generator/lighting/callback requests. Parent shell/coallocation and external borrowed resources remain separately supplied terms."}};
  AllocatorSourcePhase root_phase(NewRootStorageEpoch(),1);
  std::vector<MemberCallbackStorageSnapshot> members;Json member_rows=Json::array();
  for(const auto *world:out.member_worlds){
    auto source=world->InspectStoredMemberCallbacks(nullptr,&application,root_phase,true);
    for(const auto *geometry:out.member_geometry)if(geometry->GetWorldForStorage()==world){
      source=world->InspectStoredMemberCallbacks(geometry,&application,root_phase,true);if(source.Supported(MemberCallbackAll))break;
    }
    std::array<size_t,MemberCallbackCells> requests{},captures{};std::array<bool,MemberCallbackCells> present{},supported{};Json unknown=Json::array();
    for(size_t i=0;i<MemberCallbackCells;++i){requests[i]=source.rows[i].requests;captures[i]=source.rows[i].capture_inline;present[i]=source.rows[i].present;supported[i]=source.rows[i].supported;if(source.unknown_types[i])unknown.push_back({{"slot",i},{"type",source.unknown_types[i]}});}
    member_rows.push_back({{"supported",source.Supported(MemberCallbackAll)},{"provided",source.provided},{"requests",requests},{"captures",captures},{"present",present},{"slot_supported",supported},{"unknown_types",unknown}});
    members.push_back(source);
  }
  std::vector<const MemberCallbackStorageSnapshot*> member_views;for(const auto &source:members)member_views.push_back(&source);
  const auto member_join=JoinMemberCallbackStorage(member_views,out.member_worlds.size(),true,true);
  report["member_callback_storage"]={{"schema","member-callback-runtime.v1"},{"status",member_join.supported?"PASS":"UNSUPPORTED"},
    {"slots_per_world",MemberCallbackCells},{"worlds",member_join.worlds},{"requested",member_join.requested},{"unique_cells",member_join.cells},{"aliases",member_join.aliases},{"rows",member_rows},
    {"whole_runtime_complete",false},{"scope","35 supplied member slots per world;shared application cells deduplicated. Active external application/I/O/UI callable histories remain separate and unknown types reject."}};
  std::vector<RootStorageSnapshot> root_sources;
  const auto capture=[&](const auto &handle){root_sources.push_back(CaptureSharedRootStorage(handle,root_phase,true));};
  capture(launcher.world);capture(launcher.geometry);capture(core.GetWorldRootForStorage());capture(core.GetGeometryRootForStorage());
  capture(application.GetWorldRootForStorage());capture(application.GetGeometryRootForStorage());
  bool root_handles_known=launcher.core && launcher.application;
  if(launcher.core){root_handles_known=root_handles_known && launcher.core->get()==&core;capture(*launcher.core);}
  if(launcher.application){root_handles_known=root_handles_known && launcher.application->get()==&application;capture(*launcher.application);}
  std::vector<const void*> section_objects;
  for(const auto *geometry:out.member_geometry){capture(geometry->GetWorldRootForStorage());capture(geometry->GetSectionLightRootForStorage());
    const auto *p=geometry->GetSectionLightRootForStorage().get();if(p && std::find(section_objects.begin(),section_objects.end(),p)==section_objects.end())section_objects.push_back(p);}
  for(const auto &source:members)if(source.held_root.present)root_sources.push_back(source.held_root);
  std::vector<const RootStorageSnapshot*> root_views;Json root_rows=Json::array();for(const auto &source:root_sources){root_views.push_back(&source);
    root_rows.push_back({{"kind",static_cast<size_t>(source.kind)},{"supported",source.supported},{"present",source.present},{"requested",source.requested},{"inline_bytes",source.inline_bytes},{"expired_weak",source.expired_weak}});}
  std::array<size_t,RootStorageClasses> root_expected{out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)],out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Core)],out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Application)],out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Geometry)],section_objects.size()};
  const auto roots=JoinRootStorage(root_phase,root_views,root_expected,root_handles_known,true);const auto root_layout=GetRootStorageLayout();
  report["root_storage"]={{"schema","root-storage-runtime.v1"},{"status",roots.supported?"PASS":"UNSUPPORTED"},{"objects",root_layout.objects},{"coallocated",root_layout.coallocated},
    {"epoch_static_inline",root_layout.epoch_static_inline},{"blocks",roots.blocks},{"expected_blocks",root_expected},{"expired",roots.expired},{"requested",roots.requested},{"rows",root_rows},
    {"whole_runtime_complete",false},{"scope","Borrowed live default-factory World/Core/Application/Geometry/SectionBindings roots and supplied weak guards in one phase. Whole reserved shells count once;embedded children overlap. Other heap children,window/CRT/stacks/GPU/process remain separate."}};
  const auto operations=application.InspectOperationStorage(root_phase,true);
  Json operation_actions=Json::array();for(const auto &a:operations.actions)operation_actions.push_back({{"kind",static_cast<size_t>(a.kind)},{"supported",a.Supported()},{"present",a.callback.present},{"implementation",a.callback.requests},{"captured",a.captured},{"total",a.Total()}});
  report["application_operations"]={{"schema","application-operation-runtime.v1"},{"status",operations.supported?"PASS":"UNSUPPORTED"},{"actions",operation_actions},
    {"runner_present",operations.runner.present},{"runner_supported",operations.runner.supported},{"runner_active",operations.runner.active},{"runner_requests",operations.runner.requests},
    {"wrappers_already_in_member_storage",operations.Wrappers()},{"additional",operations.Additional()},{"total",operations.Total()},{"whole_runtime_complete",false},
    {"scope","Six known application actions and actual owned operation runner/request;function backing overlaps member_callback_storage. Captured backing and runner are additional. UI shells,outer widget callbacks,I/O/external job scopes and process remain separate."}};
  report["gui_context_storage"]=GuiContextStorageJson(application.GetGui().InspectOwnedStorage());
  report["main_menu_storage"]=MainMenuStorageJson(application.InspectMainMenuStorage());
  report["hud_storage"]=InGameHudStorageJson(application.InspectHudStorage());
  report["console_storage"]=ConsoleStorageJson(application.InspectConsoleStorage());
  report["death_screen_storage"]=DeathScreenStorageJson(application.InspectDeathScreenStorage());
  report["player_window_storage"]=PlayerWindowStorageJson(application.InspectPlayerWindowStorage());
  report["palette_grid_storage"]=PaletteGridStorageJson(application.InspectPaletteGridStorage());
  const auto worldgen_palette = application.InspectWorldGenPaletteStorage();
  report["worldgen_palette_storage"] = {
      {"schema", "worldgen-palette-owner.v1"},
      {"status", worldgen_palette.supported ? "PASS" : "UNSUPPORTED"},
      {"present", worldgen_palette.present}, {"visible", worldgen_palette.visible},
      {"bindings_match", worldgen_palette.bindingsMatch}, {"widgets", worldgen_palette.widgets},
      {"screen_bytes", worldgen_palette.screenBytes}, {"widget_bytes", worldgen_palette.widgetBytes},
      {"children_bytes", worldgen_palette.childrenBytes}, {"tab_bytes", worldgen_palette.tabBytes},
      {"text_bytes", worldgen_palette.textBytes}, {"callback_bytes", worldgen_palette.callbackBytes},
      {"preview_bytes", worldgen_palette.previewBytes}, {"content_grid_bytes", worldgen_palette.grid.contentBytes},
      {"picker_grid_bytes", worldgen_palette.grid.pickerBytes}, {"requested", worldgen_palette.totalBytes},
      {"preview_texture_present", worldgen_palette.previewTexturePresent}, {"whole_runtime_complete", false}};
  const auto preview = application.InspectContentPreviewStorage();
  const auto previewOwners = out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::Preview)];
  const bool previewJoined = preview.supported && previewOwners == size_t(preview.present);
  report["content_preview_storage"] = {
      {"schema", "content-preview-owner.v1"}, {"status", previewJoined ? "PASS" : "UNSUPPORTED"},
      {"present", preview.present}, {"bindings_match", preview.bindingsMatch}, {"owners", previewOwners},
      {"renderer_bytes", preview.rendererBytes}, {"vertex_buffer_bytes", preview.vertexBytes},
      {"index_buffer_bytes", preview.indexBytes}, {"vertex_array_present", bool(preview.vertexArray)},
      {"framebuffer_present", bool(preview.framebuffer)}, {"target_size", preview.targetSize},
      {"color_texels", preview.colorTexels}, {"depth_samples", preview.depthSamples},
      {"shader_present", preview.shaderPresent}, {"creature_source_present", preview.creatureSourcePresent},
      {"item_source_present", preview.itemSourcePresent}, {"whole_runtime_complete", false}};
  const auto icons = application.InspectObjectIconStorage();
  const auto iconOwners = out.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::ObjectIcon)];
  const bool iconsJoined = icons.supported && iconOwners == size_t(icons.present);
  report["object_icon_storage"] = {
      {"schema", "object-icon-cache-owner.v1"}, {"status", iconsJoined ? "PASS" : "UNSUPPORTED"},
      {"present", icons.present}, {"owners", iconOwners}, {"bindings_match", icons.bindingsMatch},
      {"source_bytes", icons.sourceBytes}, {"cache_bytes", icons.cacheBytes}, {"cpu_bytes", icons.cpuBytes},
      {"object_table_bytes", icons.objectTableBytes}, {"block_table_bytes", icons.blockTableBytes},
      {"object_queue_bytes", icons.objectQueueBytes}, {"block_queue_bytes", icons.blockQueueBytes},
      {"object_icons", icons.objectIcons}, {"block_icons", icons.blockIcons},
      {"pending_objects", icons.pendingObjects}, {"pending_blocks", icons.pendingBlocks},
      {"vertex_buffer_bytes", icons.vertexBytes}, {"index_buffer_bytes", icons.indexBytes},
      {"target_texels", icons.targetTexels}, {"depth_samples", icons.depthSamples}, {"cached_texels", icons.cachedTexels},
      {"atlas_fallback", icons.atlasFallback}, {"shader_present", icons.shaderPresent},
      {"icon_service_present", icons.servicePresent}, {"whole_runtime_complete", false}};
  const auto diskIcons = application.InspectInventoryIconStorage();
  report["inventory_icon_service_storage"] = {
      {"schema", "inventory-icon-service-owner.v1"}, {"status", diskIcons.supported ? "PASS" : "UNSUPPORTED"},
      {"present", diskIcons.present}, {"initialized", diskIcons.initialized}, {"dirty", diskIcons.dirty},
      {"bindings_match", diskIcons.bindingsMatch}, {"factory_owner", diskIcons.factoryOwner}, {"aliases", diskIcons.aliases},
      {"service_bytes", diskIcons.serviceBytes}, {"service_request", diskIcons.serviceRequest},
      {"readback_state_bytes", diskIcons.readbackStateBytes}, {"path_bytes", diskIcons.pathBytes},
      {"manifest_bytes", diskIcons.manifestBytes}, {"job_bytes", diskIcons.jobBytes}, {"cpu_bytes", diskIcons.cpuBytes},
      {"entries", diskIcons.entries}, {"jobs", diskIcons.jobs}, {"pack_buffer_bytes", diskIcons.packBufferBytes},
      {"whole_runtime_complete", false}};
  const auto entityIcons = application.InspectCreatureItemIconStorage();
  const auto iconTable = [](const IconTextureTableStorage &table) -> nlohmann::json
  {
    return {{"cpu_bytes", table.cpuBytes}, {"owned", table.owned}, {"borrowed", table.borrowed},
            {"owned_texels", table.ownedTexels}};
  };
  const auto iconPreview = [](const PreviewRendererStorage &preview, size_t aliases) -> nlohmann::json
  {
    const auto &surface = preview.surface;
    return {{"present", preview.present}, {"supported", preview.supported},
            {"aliases", aliases}, {"factory_owner", preview.factoryOwner},
            {"cpu_bytes", preview.cpuBytes}, {"request_bytes", preview.requestBytes},
            {"table_bytes", preview.tableBytes}, {"asset_references", preview.assetReferences},
            {"meshes", preview.meshes}, {"vertex_buffer_bytes", preview.vertexBytes},
            {"index_buffer_bytes", preview.indexBytes}, {"scratch_vertex_bytes", preview.scratchVertexBytes},
            {"scratch_index_bytes", preview.scratchIndexBytes}, {"target_texels", preview.targetTexels},
            {"depth_samples", preview.depthSamples}, {"fallback_color_texels", preview.fallbackColorTexels},
            {"resolve_framebuffer", bool(preview.resolveFramebuffer)}, {"surface_assets", surface.assets},
            {"surface_cpu_bytes", surface.cpuBytes}, {"surface_table_bytes", surface.tableBytes},
            {"surface_offset_bytes", surface.offsetBytes}, {"surface_vertex_bytes", surface.vertexBytes},
            {"surface_index_bytes", surface.indexBytes}};
  };
  const auto &creatureIcons = entityIcons.creature;
  const auto &itemIcons = entityIcons.item;
  report["creature_item_icon_storage"] = {
      {"schema", "creature-item-icon-storage.v1"}, {"status", entityIcons.supported ? "PASS" : "UNSUPPORTED"},
      {"bindings_match", entityIcons.bindingsMatch}, {"cpu_bytes", entityIcons.cpuBytes},
      {"creature", {{"present", creatureIcons.present}, {"cpu_bytes", creatureIcons.cpuBytes},
          {"cache_bytes", creatureIcons.cacheBytes}, {"queue_bytes", creatureIcons.queueBytes},
          {"pending", creatureIcons.pending}, {"species", iconTable(creatureIcons.species)},
          {"skins", iconTable(creatureIcons.skins)}}},
      {"item", {{"present", itemIcons.present}, {"cpu_bytes", itemIcons.cpuBytes},
          {"cache_bytes", itemIcons.cacheBytes}, {"queue_bytes", itemIcons.queueBytes},
          {"fingerprint_bytes", itemIcons.fingerprintBytes}, {"state_bytes", itemIcons.stateBytes},
          {"fingerprints", itemIcons.fingerprints}, {"state_requests", itemIcons.stateRequests},
          {"pending", itemIcons.pending}, {"icons", iconTable(itemIcons.icons)}}},
      {"creature_preview", iconPreview(entityIcons.creaturePreview, entityIcons.creaturePreviewAliases)},
      {"item_preview", iconPreview(entityIcons.itemPreview, entityIcons.itemPreviewAliases)},
      {"whole_runtime_complete", false}};
  const auto status = UStatusEffectCatalog::InspectPublishedStorage();
  report["status_catalog_storage"] = {
      {"schema", "status-effect-catalog-owner.v1"}, {"status", status.supported ? "PASS" : "UNSUPPORTED"},
      {"inline_bytes", status.inlineBytes}, {"table_bytes", status.tableBytes},
      {"definitions", status.definitions}, {"published", status.published},
      {"files_attempted", status.filesAttempted}, {"builtins_ready", status.builtinsReady}};
  const auto modelStorage = [](const VisualAssetStorage &asset) -> nlohmann::json
  {
    return {{"supported", asset.supported}, {"inline_bytes", asset.inlineBytes},
            {"cpu_bytes", asset.cpuBytes}, {"table_bytes", asset.tableBytes},
            {"control_bytes", asset.controlBytes}, {"payload_bytes", asset.payloadBytes},
            {"live", asset.live}, {"expired", asset.expired}, {"negative", asset.negative}, {"aliases", asset.aliases}};
  };
  VisualAssetCensusResult retainedVisuals;
  VisualAssetStorage gltf, bones, rigid, items;
  {
    VisualAssetCensus visual;
    gltf = CreatureGltfCache::Instance().InspectOwnedStorage(&visual);
    bones = CreatureBoneSkeletonCache::Instance().InspectOwnedStorage(&visual);
    rigid = CreatureRigidModelCache::Instance().InspectOwnedStorage(&visual);
    items = InspectItemSurfaceAssetStorage(&visual);
    for (const auto *world : out.member_worlds)
    {
      if (const auto &defs = world->GetCreatureDefinitionStorage())
        defs->InspectVisualAssets(visual, VisualAssetOwner::World);
      world->ForEachCreature([&](const UCreature &creature)
      {
        if (const auto *representation = creature.GetVisual())
          representation->InspectVisualAssets(visual);
      });
    }
    for (const auto *renderer : out.member_geometry)
      renderer->InspectVisualAssets(visual);
    application.InspectVisualAssets(visual);
    WornEquipmentDrawer::InspectVisualAssets(visual);
    UFpViewmodelRenderer::InspectLegacyVisualAssets(visual);
    if (const auto &diagnostic = CreatureQaFootAssets())
      for (const auto &entry : *diagnostic)
        visual.Add(entry.second.owner, VisualAssetOwner::Diagnostic);
    retainedVisuals = visual.Result();
  }
  Json retainedRows = Json::object();
  constexpr std::array kinds{"rigid", "gltf", "bones", "items", "legacy_parts", "palette", "item_draw"};
  constexpr std::array rolesForAssets{"cache", "world", "preview", "queue", "viewmodel", "equipment", "diagnostic"};
  for (size_t kind = 0; kind < retainedVisuals.rows.size(); ++kind)
  {
    const auto &row = retainedVisuals.rows[kind];
    Json references = Json::object();
    for (size_t role = 0; role < row.references.size(); ++role)
      references[rolesForAssets[role]] = row.references[role];
    retainedRows[kinds[kind]] = {{"cached", row.cached}, {"detached", row.detached},
        {"shared", row.shared}, {"references", std::move(references)},
        {"detached_control_bytes", row.detachedControlBytes},
        {"detached_payload_bytes", row.detachedPayloadBytes}};
  }
  report["retained_visual_asset_storage"] = {
      {"schema", "retained-visual-asset-storage.v1"},
      {"status", retainedVisuals.supported ? "PASS" : "UNSUPPORTED"}, {"assets", std::move(retainedRows)},
      {"queue_bytes", retainedVisuals.queueBytes}, {"legacy_table_bytes", retainedVisuals.legacyTableBytes},
      {"legacy_inline_bytes", retainedVisuals.legacyInlineBytes}, {"nonowning_meshes", retainedVisuals.nonowningMeshes},
      {"observer_bytes", retainedVisuals.observerBytes}, {"observer_inline_bytes", sizeof(VisualAssetCensus)},
      {"unmeasured_references", retainedVisuals.unmeasuredReferences}, {"whole_runtime_complete", false}};
  const auto visualFiles = GetVisualLoaderSources();
  constexpr std::array loaderNames{"gltf", "skeleton", "rigid", "item"};
  Json loaderRows = Json::object();
  bool visualFilesSupported = StlSourceStorage::SupportedProfile();
  for (size_t i = 0; i < visualFiles.size(); ++i)
  {
    const auto &row = visualFiles[i];
    visualFilesSupported &= !row.unclosedDocuments && !row.unsupportedSamples;
    loaderRows[loaderNames[i]] = {
        {"attempts", row.attempts}, {"completed", row.completed},
        {"active", row.active}, {"peak_active", row.peakActive},
        {"documents", row.documents}, {"unclosed_documents", row.unclosedDocuments},
        {"unsupported_samples", row.unsupportedSamples}, {"binary_reads", row.binaryReads},
        {"json_bytes_max", row.jsonBytesMax}, {"json_heap_upper_max", row.jsonHeapUpperMax},
        {"binary_bytes_max", row.binaryBytesMax}, {"binary_backing_max", row.binaryBackingMax},
        {"scratch_backing_max", row.scratchBackingMax}, {"path_units_max", row.pathUnitsMax},
        {"resource_roots", row.resourceRoots}, {"resource_files", row.resourceFiles},
        {"resource_path_backing_max", row.resourcePathBackingMax}};
  }
  report["visual_loader_sources"] = {
      {"schema", "visual-loader-sources.v1"},
      {"status", visualFilesSupported ? "PASS" : "UNSUPPORTED"},
      {"loaders", std::move(loaderRows)}, {"counters_inline_bytes", sizeof(visualFiles) + sizeof(std::mutex)},
      {"whole_runtime_complete", false}};
  const auto visualFilesystem = GetVisualFilesystemSources();
  Json filesystemRows = Json::object();
  bool visualFilesystemSupported = StlSourceStorage::SupportedProfile();
  for (size_t i = 0; i < visualFilesystem.size(); ++i)
  {
    const auto &row = visualFilesystem[i];
    visualFilesystemSupported &= !row.unsupported;
    filesystemRows[VisualFilesystemNames[i]] = {
        {"calls", row.calls}, {"finished", row.finished}, {"failed", row.failed},
        {"active", row.active}, {"peak_active", row.peakActive}, {"unsupported", row.unsupported},
        {"input_units_max", row.inputUnitsMax}, {"output_units_max", row.outputUnitsMax},
        {"output_backing_max", row.outputBackingMax}, {"ordinary_upper_max", row.internalUpperMax}};
  }
  report["visual_filesystem_sources"] = {
      {"schema", "visual-filesystem-sources.v1"},
      {"status", visualFilesystemSupported ? "PASS" : "UNSUPPORTED"},
      {"operations", std::move(filesystemRows)}, {"probe_inline_bytes", sizeof(VisualFilesystemSource)},
      {"counters_inline_bytes", sizeof(visualFilesystem) + sizeof(std::mutex)},
      {"whole_runtime_complete", false}};
  const auto fileBuffers = GetVisualFileBuffers();
  report["visual_file_buffers"] = {
      {"schema", "visual-file-buffers.v1"},
      {"status", StlSourceStorage::SupportedProfile() && !fileBuffers.fallback &&
          !fileBuffers.closeFailures ? "PASS" : "UNSUPPORTED"},
      {"opened", fileBuffers.opened}, {"configured", fileBuffers.configured},
      {"closed", fileBuffers.closed}, {"fallback", fileBuffers.fallback},
      {"unretired_after_close", fileBuffers.closeFailures},
      {"active_streams", fileBuffers.active}, {"peak_open_streams", fileBuffers.peakActive},
      {"active_read_buffer_bytes", fileBuffers.activeBufferBytes},
      {"peak_read_buffer_bytes", fileBuffers.peakBufferBytes},
      {"owned_array_bytes_per_stream", VisualModelInputFile::BufferBytes},
      {"inline_file_owner_bytes", sizeof(VisualModelInputFile)},
      {"additional_counters_inline_bytes", sizeof(fileBuffers)}, {"whole_runtime_complete", false}};
  const auto crtFiles = InspectWindowsCrtFileTables();
  report["crt_file_tables"] = {
      {"schema", "windows-crt-file-tables.v1"}, {"status", crtFiles.supported ? "PASS" : "UNSUPPORTED"},
      {"stream_slots", crtFiles.streamSlots}, {"file_slot_bytes", crtFiles.fileSlotBytes},
      {"stream_pointer_table_bytes", crtFiles.streamPointerTableBytes},
      {"dynamic_file_slots_upper", crtFiles.dynamicFileSlotsUpper},
      {"descriptor_blocks", crtFiles.descriptorBlocks}, {"descriptors_per_block", crtFiles.descriptorsPerBlock},
      {"descriptor_bytes", crtFiles.descriptorBytes}, {"descriptor_backing_upper", crtFiles.descriptorBackingUpper},
      {"table_statics_bytes", crtFiles.staticsBytes}, {"current_requested_heap_upper", crtFiles.currentRequestedHeapUpper},
      {"resize_pointer_tables_upper", crtFiles.resizePointerTablesUpper}, {"requested_heap_upper", crtFiles.requestedHeapUpper},
      {"process_handles", crtFiles.processHandles}, {"process_handles_observed", crtFiles.processHandlesObserved},
      {"whole_runtime_complete", false}};
  const auto memory = ReadWindowsProcessMemory();
  report["windows_process_resources"] = {
      {"schema", "windows-process-resources.v1"},
      {"status", memory.observed && crtFiles.processHandlesObserved ? "PASS" : "UNSUPPORTED"},
      {"handles", crtFiles.processHandles}, {"handles_observed", crtFiles.processHandlesObserved},
      {"memory_observed", memory.observed}, {"working_set_bytes", memory.workingSet},
      {"peak_working_set_bytes", memory.peakWorkingSet}, {"private_commit_bytes", memory.privateCommit},
      {"peak_private_commit_bytes", memory.peakPrivateCommit},
      {"paged_pool_quota_bytes", memory.pagedPoolQuota}, {"peak_paged_pool_quota_bytes", memory.peakPagedPoolQuota},
      {"nonpaged_pool_quota_bytes", memory.nonpagedPoolQuota},
      {"peak_nonpaged_pool_quota_bytes", memory.peakNonpagedPoolQuota},
      {"per_object_charge_observed", false}, {"whole_runtime_complete", false}};
  report["native_thread_stacks"] = DescribeNativeThreadStacks();
  report["windows_gpu_memory"] = DescribeWindowsGpuMemory(ReadWindowsGpuMemory());
  const auto itemScenes = GetItemSceneTraversals();
  const auto &scene = itemScenes.largest;
  report["item_scene_traversal"] = {
      {"schema", "item-scene-traversal.v1"},
      {"status", StlSourceStorage::SupportedProfile() && !itemScenes.unsupported ?
          "PASS" : "UNSUPPORTED"}, {"started", itemScenes.started},
      {"completed", itemScenes.completed}, {"unsupported", itemScenes.unsupported},
      {"largest", {{"nodes", scene.nodes}, {"visited", scene.visited},
                   {"max_depth", scene.maxDepth}, {"frame_capacity", scene.frameCapacity},
                   {"visited_requested", scene.visitedRequested},
                   {"frame_growth_requested", scene.frameGrowthRequested},
                   {"peak_requested", scene.peakRequested}}},
      {"inline_cursor_bytes", sizeof(ItemSceneCursor)},
      {"counters_inline_bytes", sizeof(itemScenes) + sizeof(std::mutex)},
      {"whole_runtime_complete", false}};
  const auto skeletonBuilds = GetSkeletonMeshBuildSnapshot();
  const auto &skeleton = skeletonBuilds.largest;
  report["skeleton_mesh_construction"] = {
      {"schema", "skeleton-mesh-construction.v1"},
      {"status", StlSourceStorage::SupportedProfile() && !skeletonBuilds.unsupported ?
          "PASS" : "UNSUPPORTED"},
      {"completed", skeletonBuilds.completed}, {"unsupported", skeletonBuilds.unsupported},
      {"largest", {{"new_requested", skeleton.requestedBytes}, {"mesh_requested", skeleton.meshBytes},
                   {"control_requested", skeleton.controlBytes}, {"empty_index_requested", skeleton.emptyIndexBytes},
                   {"explicit_cube_arrays", skeleton.explicitScratchBytes}, {"bones", skeleton.bones},
                   {"cubes", skeleton.cubes}}},
      {"counters_inline_bytes", sizeof(skeletonBuilds) + sizeof(std::mutex)},
      {"whole_runtime_complete", false}};
  const auto creatureGpu = CreatureMeshGpuCache::Instance().InspectOwnedStorage();
  report["shared_visual_source_storage"] = {
      {"schema", "shared-visual-source-storage.v1"},
      {"status", gltf.supported && bones.supported && rigid.supported && items.supported && creatureGpu.supported ? "PASS" : "UNSUPPORTED"},
      {"gltf", modelStorage(gltf)}, {"bones", modelStorage(bones)}, {"items", modelStorage(items)},
      {"rigid", modelStorage(rigid)},
      {"creature_gpu", {{"inline_bytes", creatureGpu.inlineBytes}, {"table_bytes", creatureGpu.tableBytes},
          {"skeletal", creatureGpu.skeletal}, {"skinned", creatureGpu.skinned},
          {"vertex_bytes", creatureGpu.vertexBytes}, {"index_bytes", creatureGpu.indexBytes}}},
      {"whole_runtime_complete", false}};
  const auto textureFields = [](const VisualTextureStorage &textures) -> Json
  {
    return {{"present", textures.present}, {"supported", textures.supported},
            {"inline_bytes", textures.inlineBytes}, {"control_bytes", textures.controlBytes}, {"table_bytes", textures.tableBytes},
            {"images", textures.images}, {"aliases", textures.aliases}, {"rgba_bytes", textures.rgbaBytes},
            {"models", textures.models}, {"linear_images", textures.linearImages}, {"linear_aliases", textures.linearAliases}};
  };
  const auto creatureTextures = core.GetCreatureTextureStorage();
  const auto itemTextures = ItemGltfTextureCache::InspectCurrentStorage();
  const auto creatureSource = creatureTextures ? creatureTextures->InspectOwnedStorage() : VisualTextureStorage{};
  const auto geometryTextures = launcher.geometry ? launcher.geometry->GetCreatureTextureStorage() : nullptr;
  const bool geometryShares = creatureTextures && geometryTextures == creatureTextures &&
      !geometryTextures.owner_before(creatureTextures) && !creatureTextures.owner_before(geometryTextures);
  report["shared_texture_source_storage"] = {
      {"schema", "shared-texture-source-storage.v1"},
      {"status", itemTextures.supported && creatureSource.supported && geometryShares ? "PASS" : "UNSUPPORTED"},
      {"geometry_shares_creature_storage", geometryShares}, {"upload_probe_thread_bytes", UVisualTextureUploadProbeScope::ThreadInlineBytes()},
      {"creature", textureFields(creatureSource)}, {"items", textureFields(itemTextures)}, {"whole_runtime_complete", false}};
  root_phase.Close();
  // An actual second UWorld verifies that the traversal counts supplied
  // diagnostic worlds separately. It never replaces the active world.
  if(std::string(phase)=="in-game"){
    CatalogApplicationDomain diagnostic;diagnostic.owner_core=&core;diagnostic.owner_application=&application;diagnostic.Core(&core);diagnostic.Application(&application);
    const auto before=diagnostic.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)];
    {UWorld auxiliary(nullptr,nullptr);diagnostic.World(&auxiliary);
      report["diagnostic_worlds_with_auxiliary"]=diagnostic.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)];
      report["diagnostic_pool_count_with_auxiliary"]=diagnostic.pools.size();
      report["diagnostic_pool_workers_with_auxiliary"]=diagnostic.workers;
      report["diagnostic_thread_upper_with_auxiliary"]=diagnostic.CallingAndPoolThreadUpper();}
    report["auxiliary_world_counted"]=diagnostic.objects[static_cast<size_t>(CatalogRuntimeDomain::Kind::World)]==before+1;
    report["auxiliary_domain_supported"]=diagnostic.Supported();
    if(!diagnostic.Supported())report["status"]="UNSUPPORTED";
  }
  std::filesystem::create_directories(directory);
  std::ofstream file(std::filesystem::path(directory)/(std::string(entrypoint)+"-"+phase+".json"));
  file<<report.dump(2)<<'\n';
  if(!file)throw std::runtime_error("Catalog runtime domain receipt write failed");
}
} // namespace cutum
