#pragma once
#include "Core/Memory/RootStorageSource.h"
#include "Render/Lighting/SectionRenderLightBindings.h"
#include <nlohmann/json.hpp>
namespace cutum {
inline nlohmann::json RunRootStorageCompositionProbe(){
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  const auto layout=GetRootStorageLayout();require(layout.supported,"pinned root profile");
  AllocatorSourcePhase phase(NewRootStorageEpoch(),1);auto owner=std::make_shared<SectionRenderLightBindings>();
  std::weak_ptr<SectionRenderLightBindings> guard=owner;
  const auto shared=CaptureSharedRootStorage(owner,phase,true),weak=CaptureWeakRootStorage(guard,phase,true);
  std::array<size_t,RootStorageClasses> expected{};expected[4]=1;
  std::vector<const RootStorageSnapshot*> views{&shared,&weak};const auto joined=JoinRootStorage(phase,views,expected,true,true);
  require(joined.supported && joined.requested==layout.coallocated[4] && owner.use_count()==1,"shared/weak same physical root");
  owner.reset();const auto expired=CaptureWeakRootStorage(guard,phase,true);std::vector<const RootStorageSnapshot*> expired_views{&expired};
  const auto retired=JoinRootStorage(phase,expired_views,expected,true,true);
  require(retired.supported && retired.expired[4]==1 && retired.requested==joined.requested,"expired weak retains full reserved block");
  require(!JoinRootStorage(phase,expired_views,expected,false,true).supported && !JoinRootStorage(phase,expired_views,expected,true,false).supported,"root coverage required");
  phase.Close();require(!JoinRootStorage(phase,expired_views,expected,true,true).supported && !CaptureWeakRootStorage(guard,phase,true).supported,"closed root phase rejected");guard.reset();
  return {{"schema","root-storage-composition.v1"},{"status","PASS"},{"objects",layout.objects},{"coallocated",layout.coallocated},
    {"epoch_static_inline",layout.epoch_static_inline},{"weak_reserved",retired.requested},{"scope","Pure common join and real empty SectionBindings lifetime;no diagnostic Core/Application construction in a running application. Unit verifies actual five constructors and allocation containment."}};
}
} // namespace cutum
