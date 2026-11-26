#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/RootSourceGlContext.h"
#include "Test/OrdinaryNewObservation.h"
#include "Test/MemberCallbackStorageFixture.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "App/Application.h"
#include <fstream>
#include <iostream>
int main(int argc,char **argv){
  using namespace cutum;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try{
    _putenv_s("CUBA_SURFACE_LIGHT_DIAGNOSTICS","1");
    RootSourceGlContext context;RefreshRenderBackendCapsFromGl();
    auto report=RunMemberCallbackStorageProbe();auto world=std::make_shared<UWorld>(nullptr,nullptr);
    auto definitions=MakePipelineStorageDefinitions();WorldOwnedStorageProbe::Prepare(*world,definitions,WorldOwnedFixtureSettings(0));CallbackRootSourceProbe::Install(*world);
    auto geometry=std::make_shared<UGeometryEngine>(world,nullptr,nullptr);CallbackRootSourceProbe::BindGeometry(*geometry);
    AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const auto before=world->InspectStoredMemberCallbacks(geometry.get(),nullptr,phase,true);
    require(before.Supported(MemberCallbackAll) && before.held_root.present && before.rows[29].present && before.rows[30].present,"real backend and conditional member/weak profiles");
    const auto references=geometry->GetSectionLightRootForStorage().use_count();bool exact=false;size_t query_attempts{};
    {observation::Scope refusal(0);const auto source=world->InspectStoredMemberCallbacks(geometry.get(),nullptr,phase,true);
      exact=source.Supported(MemberCallbackAll) && source.Requested()==before.Requested() && source.held_root.allocation==before.held_root.allocation;
      query_attempts=observation::State.attempts;}
    require(exact && !query_attempts && geometry->GetSectionLightRootForStorage().use_count()==references,"member query allocate0 and no owner changes");
    auto other=std::make_shared<UWorld>(nullptr,nullptr);require(!other->InspectStoredMemberCallbacks(geometry.get(),nullptr,phase,true).Supported(MemberCallbackAll),"foreign geometry world rejected");
    UApplication application(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
    require(application.InspectStoredActionCallbacks(true).Supported((uint64_t{1}<<33)|(uint64_t{1}<<34)),"actual empty application action cells");
    report["geometry_callbacks_present"]={before.rows[31].present,before.rows[32].present};report["geometry_weak_root_requests"]=before.held_root.requested;
    report["query_allocate0"]=true;report["owner_references_unchanged"]=true;report["selected_final_live"]=observation::State.live;
    phase.Close();geometry.reset();world.reset();std::ofstream file(argc>1?argv[1]:"member-callback-storage.json");file<<report.dump(2)<<'\n';require(bool(file),"member report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
