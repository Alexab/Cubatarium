#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/RootSourceGlContext.h"
#include "Test/RootAllocationObservation.h"
#include "Test/RootStorageCompositionFixture.h"
#include "World/Core/World.h"
#include "App/Core.h"
#include "App/Application.h"
#include "Render/Engine/GeometryEngine.h"
#include <fstream>
#include <iostream>
namespace {
using namespace cutum;
void Require(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class Factory> nlohmann::json ObserveFactory(Factory factory){
  using Owner=decltype(factory());using T=typename Owner::element_type;namespace obs=root_observation;
  {auto warm=factory();}{auto warm=factory();}
  size_t attempts{},selected{},peak{};bool query_ok{};
  {
    obs::Scope measured;auto owner=factory();attempts=obs::State.attempts;
    AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const auto snapshot=CaptureSharedRootStorage(owner,phase,true);
    const auto *physical=obs::Holding(owner.get(),sizeof(T));selected=snapshot.requested;peak=obs::State.peak;
    Require(snapshot.supported && physical && physical->bytes==snapshot.requested && physical->address==snapshot.allocation,"actual root physical request containment");
    const auto before=obs::State.attempts;const auto refs=owner.use_count();obs::State.fail_after=before;
    const auto repeat=CaptureSharedRootStorage(owner,phase,true);query_ok=repeat.supported && repeat.allocation==snapshot.allocation && obs::State.attempts==before && owner.use_count()==refs;
    obs::State.fail_after=SIZE_MAX;
    std::weak_ptr<T> guard=owner;const auto words=RootOwnerWords(guard);owner.reset();
    const auto weak=CaptureWeakRootStorage(guard,phase,true);physical=obs::Holding(reinterpret_cast<const void*>(words[0]),sizeof(T));
    Require(weak.supported && weak.expired_weak && physical && physical->bytes==selected && obs::State.live==selected && obs::State.blocks==1,"destroyed root retains exactly one weak coallocation");
    const auto weak_attempts=obs::State.attempts;obs::State.fail_after=weak_attempts;
    Require(CaptureWeakRootStorage(guard,phase,true).supported && obs::State.attempts==weak_attempts,"expired root query allocate0");obs::State.fail_after=SIZE_MAX;
    phase.Close();guard.reset();Require(!obs::State.live && !obs::State.blocks,"final weak root request release0");
  }
  for(size_t prefix=0;prefix<attempts;++prefix){
    obs::Scope measured(prefix);bool refused=false;try{auto owner=factory();}catch(const std::bad_alloc&){refused=true;}
    obs::State.fail_after=SIZE_MAX;Require(refused && !obs::State.live && !obs::State.blocks,"root constructor refusal unwinds0");
    auto retry=factory();AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const auto snapshot=CaptureSharedRootStorage(retry,phase,true);const auto *physical=obs::Holding(retry.get(),sizeof(T));
    Require(snapshot.supported && physical && snapshot.requested==physical->bytes && snapshot.requested==selected,"same-input root constructor retry");phase.Close();retry.reset();Require(!obs::State.live && !obs::State.blocks,"root retry destruction0");
  }
  Require(query_ok,"root query no allocation or reference change");
  return {{"kind",static_cast<size_t>(RootStorageKind<T>)},{"selected_request",selected},{"construction_attempts",attempts},{"refusal_prefixes",attempts},{"whole_fixture_peak",peak},{"query_allocate0",query_ok},{"weak_reserved_exact",true}};
}
} // namespace
int main(int argc,char **argv){
  try{
    RootSourceGlContext context;auto report=RunRootStorageCompositionProbe();nlohmann::json cases=nlohmann::json::array();
    cases.push_back(ObserveFactory([]{return std::make_shared<UWorld>(nullptr,nullptr);}));
    cases.push_back(ObserveFactory([]{return std::make_shared<UCore>(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);}));
    cases.push_back(ObserveFactory([]{return std::make_shared<UApplication>(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);}));
    cases.push_back(ObserveFactory([]{return std::make_shared<UGeometryEngine>(nullptr,nullptr,nullptr);}));
    cases.push_back(ObserveFactory([]{return std::make_shared<SectionRenderLightBindings>();}));
    AllocatorSourcePhase phase(NewRootStorageEpoch(),1);auto owner=std::make_shared<UWorld>(nullptr,nullptr);
    auto unsupported=std::shared_ptr<UWorld>(new UWorld(nullptr,nullptr));
    auto allocated=std::allocate_shared<UWorld>(std::allocator<UWorld>{},nullptr,nullptr);
    auto alias=std::shared_ptr<UWorld>(owner,reinterpret_cast<UWorld*>(reinterpret_cast<uintptr_t>(owner.get())+8));
    auto null_alias=std::shared_ptr<UWorld>(owner,static_cast<UWorld*>(nullptr));
    Require(!CaptureSharedRootStorage(unsupported,phase,true).supported && !CaptureSharedRootStorage(allocated,phase,true).supported &&
      !CaptureSharedRootStorage(alias,phase,true).supported && !CaptureSharedRootStorage(null_alias,phase,true).supported && !CaptureSharedRootStorage(owner,phase,false).supported,"unknown factory/deleter/allocator/alias/history controls");
    const auto shared=CaptureSharedRootStorage(owner,phase,true);std::array<size_t,RootStorageClasses> expected{};expected[0]=1;
    std::vector<const RootStorageSnapshot*> views{&shared};for(int i=0;i<64;++i)views.push_back(&shared);
    const auto joined=JoinRootStorage(phase,views,expected,true,true);Require(joined.supported && joined.requested==shared.requested,"64 root aliases counted once");
    auto conflict=shared;conflict.requested++;auto bad=views;bad.push_back(&conflict);Require(!JoinRootStorage(phase,bad,expected,true,true).supported,"conflicting root identity rejected");
    UWorld stack(nullptr,nullptr);const auto stack_source=CaptureDirectRootStorage(&stack,RootStorageOrigin::Stack,phase,true);
    const auto child=CaptureEmbeddedRootStorage(&stack,&stack.GetBlockWorld(),phase,true);std::vector<const RootStorageSnapshot*> local{&stack_source,&child};std::array<size_t,RootStorageClasses> none{};
    const auto local_join=JoinRootStorage(phase,local,none,true,true);Require(local_join.supported && !local_join.requested && local_join.stack_inline==sizeof(UWorld) && local_join.embedded_inline==sizeof(UBlockWorld),"stack parent and embedded child overlap");
    auto missing_parent=local;missing_parent.erase(missing_parent.begin());Require(!JoinRootStorage(phase,missing_parent,none,true,true).supported,"embedded parent coverage required");
    auto unique=std::make_unique<UWorld>(nullptr,nullptr);const auto direct=CaptureDirectRootStorage(unique.get(),RootStorageOrigin::OrdinaryUnique,phase,true);
    Require(direct.supported && direct.requested==sizeof(UWorld) && !CaptureDirectRootStorage(unique.get(),RootStorageOrigin::Unknown,phase,true).supported,"explicit unique source and unknown origin");
    auto huge=shared;huge.requested=SIZE_MAX;const auto second=CaptureDirectRootStorage(unique.get(),RootStorageOrigin::OrdinaryUnique,phase,true);auto two=expected;two[0]=2;
    std::vector<const RootStorageSnapshot*> overflow{&huge,&second};Require(JoinRootStorage(phase,overflow,two,true,true).overflow,"root checked overflow");
    std::array<size_t,RootStorageClasses> empty{};Require(!JoinRootStorage(phase,views,empty,true,true).supported,"root participant source count");
    AllocatorSourcePhase other_phase(NewRootStorageEpoch(),1);Require(!JoinRootStorage(other_phase,views,expected,true,true).supported,"unique phase epochs reject old views");
    Require(!root_observation::State.live && !root_observation::State.blocks,"all observed source requests retired");
    phase.Close();report["ordinary_cases"]=cases;report["alias_origin_domain_overflow_controls"]=true;report["selected_final_live"]=root_observation::State.live;
    std::ofstream file(argc>1?argv[1]:"root-storage-source.json");file<<report.dump(2)<<'\n';Require(bool(file),"root report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
