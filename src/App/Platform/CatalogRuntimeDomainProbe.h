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
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

namespace cutum
{
struct CatalogApplicationDomain final : CatalogRuntimeDomain
{
  AllocatorOwnerCensus AllocationOwners;
  using CatalogRuntimeDomain::Geometry;
  void Core(const UCore *v){if(Enter(Kind::Core,v))v->InspectCatalogDomain(*this);}
  void Application(const UApplication *v){if(Enter(Kind::Application,v))v->InspectCatalogDomain(*this);}
  void World(const UWorld *v){if(Enter(Kind::World,v)){AllocationOwners.Manager(v->GetBlockWorld().GetChunkManager(),nullptr,false,!ActiveScopedSharedResource);v->InspectCatalogDomain(*this);v->GetBlockWorld().InspectCatalogRoots(*this);}}
  void Geometry(const UGeometryEngine *v){if(Enter(Kind::Geometry,v)){v->InspectCatalogDomain(*this);v->InspectCatalogRoots(*this);}}
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
};
inline void WriteCatalogRuntimeDomain(const UCore &core,const UApplication &application,
    CatalogLauncherRootsView launcher,const char *entrypoint,const char *phase)
{
  const char *directory=std::getenv("CUBA_CATALOG_RUNTIME_DOMAIN");if(!directory || !*directory)return;
  using Json=nlohmann::json;CatalogApplicationDomain out;
  out.Core(&core);out.Application(&application);
  out.World(launcher.world.get());out.Geometry(launcher.geometry.get());
  out.BlockStorage(launcher.blocks,"launcher");out.TextureBases(launcher.bases);out.TextureCubes(launcher.cubes);out.Library(launcher.objects);
  const auto content=CaptureWorldGenContentSnapshot();out.Content(content);out.CallingThread();out.ActiveDirectory();
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
  // An actual second UWorld verifies that the traversal counts supplied
  // diagnostic worlds separately. It never replaces the active world.
  if(std::string(phase)=="in-game"){
    CatalogApplicationDomain diagnostic;diagnostic.Core(&core);diagnostic.Application(&application);
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
