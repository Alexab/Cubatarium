#pragma once
#include "App/Core.h"
#include "App/Application.h"
#include "Render/Engine/GeometryEngine.h"
#include "Gui/Core/GuiIconSource.h"
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
