#pragma once
#include "Test/WorldOwnedStorageFixture.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Mesh/WorldMeshService.h"
#include "Render/Engine/GeometryEngine.h"
namespace cutum {
struct CallbackRootSourceProbe {
  static void Install(UWorld &world){
    auto &stream=*world.Streaming;
    stream.EnsureStreamer(world.BlockWorld,*world.BlockRegistry,0,world.GetProceduralSettings());
    stream.InitStreamerCallbacks(world);
    auto &emerge=stream.GetEmergeCoordinator();emerge.SoftDeferPolicy.world=&world;
    emerge.InstallSoftDeferCallbacks(*world.MeshService);
    world.EnterLitGateActive=true;world.SyncEnterVisualGateQuiesceFlags();
  }
  static void BindGeometry(UGeometryEngine &geometry){geometry.EnsureRenderBackendsBound();}
  static bool ForeignWorld(UWorld &world,UWorld &foreign){
    AllocatorSourcePhase phase(NewRootStorageEpoch(),1);
    const auto source=world.Streaming->InspectStoredCallbacks(foreign,true);
    return !source.Supported((uint64_t{1}<<14)-1);
  }
  static bool ForeignPolicy(UWorld &world,UWorld &foreign){
    auto &emerge=world.Streaming->GetEmergeCoordinator();emerge.SoftDeferPolicy.world=&foreign;
    AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const bool rejected=!world.InspectStoredMemberCallbacks(nullptr,nullptr,phase,true).Supported(MemberCallbackAll);
    emerge.SoftDeferPolicy.world=&world;return rejected;
  }
};
inline nlohmann::json RunMemberCallbackStorageProbe(){
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  UWorld world(nullptr,nullptr),other(nullptr,nullptr);
  auto definitions=MakePipelineStorageDefinitions();WorldOwnedStorageProbe::Prepare(world,definitions,WorldOwnedFixtureSettings(0));
  AllocatorSourcePhase phase(NewRootStorageEpoch(),1);
  const auto cold=world.InspectStoredMemberCallbacks(nullptr,nullptr,phase,true);require(cold.Supported(MemberCallbackAll),"fresh member slots supported");
  CallbackRootSourceProbe::Install(world);const auto active=world.InspectStoredMemberCallbacks(nullptr,nullptr,phase,true);
  require(active.Supported(MemberCallbackAll),"actual streamer/scheduler/emerge installation profile");
  require(CallbackRootSourceProbe::ForeignWorld(world,other) && CallbackRootSourceProbe::ForeignPolicy(world,other),"foreign capture owner/policy rejected");
  std::vector<const MemberCallbackStorageSnapshot*> views{&active};const auto joined=JoinMemberCallbackStorage(views,1,true,true);
  auto aliases=views;for(int i=0;i<64;++i)aliases.push_back(&active);
  const auto repeated=JoinMemberCallbackStorage(aliases,1,true,true);
  auto conflict=active;conflict.rows[static_cast<size_t>(MemberCallbackCell::Load)].owner++;
  auto conflicting=views;conflicting.push_back(&conflict);
  auto huge=active;huge.rows[0].requests=SIZE_MAX;huge.rows[2].requests=1;std::vector<const MemberCallbackStorageSnapshot*> overflow{&huge};
  require(joined.supported && repeated.supported && repeated.requested==joined.requested && !JoinMemberCallbackStorage(conflicting,1,true,true).supported &&
    !JoinMemberCallbackStorage(views,2,true,true).supported && !JoinMemberCallbackStorage(views,1,false,true).supported &&
    !JoinMemberCallbackStorage(views,1,true,false).supported && JoinMemberCallbackStorage(overflow,1,true,true).overflow,"member identity/alias/domain/overflow controls");
  size_t calls{};world.GetMeshService().GetCache().SetOnPackedRepresentationSwitchFn([padding=std::array<char,1024>{},&calls](glm::ivec3){++calls;(void)padding;});
  const bool unknown=!world.InspectStoredMemberCallbacks(nullptr,nullptr,phase,true).Supported(MemberCallbackAll) && !calls;
  world.GetMeshService().GetCache().SetOnPackedRepresentationSwitchFn({});require(unknown,"unknown callback not invoked");
  phase.Close();require(!world.InspectStoredMemberCallbacks(nullptr,nullptr,phase,true).Supported(MemberCallbackAll),"closed member phase rejected");
  std::array<size_t,MemberCallbackCells> captures{},requests{};std::array<bool,MemberCallbackCells> present{};
  for(size_t i=0;i<captures.size();++i){captures[i]=active.rows[i].capture_inline;requests[i]=active.rows[i].requests;present[i]=active.rows[i].present;}
  return {{"schema","member-callback-storage.v1"},{"status","PASS"},{"slots",MemberCallbackCells},{"captures",captures},{"requests",requests},{"present",present},
    {"selected_requested",joined.requested},{"unique_cells",joined.cells},{"reference_owners_match",true},{"unknown_callback_not_invoked",unknown},
    {"scope","Actual 35 member slots;nontrivial application actions,queued I/O/UI jobs and external owner graphs remain separate. Geometry binding is additionally checked by real-context unit/native observations."}};
}
} // namespace cutum
