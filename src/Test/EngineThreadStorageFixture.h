#pragma once
#include "Core/Memory/EngineThreadStorage.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum {
struct EngineThreadExercise {
  std::array<size_t,8> retained{};EngineThreadStorageSnapshot final;
  size_t checks{};bool fresh{},capacity_retained{},query_unchanged{};
};
inline EngineThreadTransitionDomain EngineThreadFixtureUpper(){return {64,2,32768,8193,32768,128,65536,65536,true,true,true};}
template<class Check> EngineThreadExercise ExerciseEngineThreadStorage(const std::string &long_name,Check check){
  const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  EngineThreadExercise out;const auto snap=[](){return InspectCallingThreadEngineStorage(true,true);};
  const auto record=[&](size_t i){const auto s=snap();require(s.supported && !s.overflow,"engine source profile supported");check(s);++out.checks;out.retained[i]=s.Total();};
  out.fresh=snap().Total()==0;require(out.fresh,"fresh engine TLS has no backing");record(0);
  auto &r=detail::CallingRenderThreadState();
  {
    auto capture=std::make_shared<BoundSectionLightCapture>();
    for(int i=0;i<64;++i)r.CpuWitnesses()[{i,0,0,0,BlockId{1}}].second=capture;
  }
  require(snap().capture_owners==1 && snap().cpu_entries==64,"capture aliases deduplicate by owner");record(1);
  for(size_t i=0;i<8192;++i)r.GpuFrames.emplace_back();record(2);
  for(int i=0;i<8193;++i)r.PreviewStates()[{i,0,0}].shown=(i&1)!=0;record(3);
  const auto before_clear=snap();r.PreviewStates().clear();record(4);
  out.capacity_retained=snap().preview_buckets==before_clear.preview_buckets && snap().requests[static_cast<size_t>(EngineThreadCell::PreviewTable)]>0;
  require(out.capacity_retained,"cleared preview retains real buckets");
  r.PreviewStates()[{1,2,3}].raw=true;
  auto &bodies=r.Environment().CelestialBodies;bodies.resize(65);bodies[0].Id=long_name;
  detail::CallingMeshPublicationThreadState().LastState=long_name;record(5);
  const auto long_snapshot=snap();bodies[0].Id="sun";detail::CallingMeshPublicationThreadState().LastState="ready";record(6);
  require(snap().requests[static_cast<size_t>(EngineThreadCell::EnvironmentNames)]==long_snapshot.requests[static_cast<size_t>(EngineThreadCell::EnvironmentNames)] &&
    snap().requests[static_cast<size_t>(EngineThreadCell::PublicationTrace)]==long_snapshot.requests[static_cast<size_t>(EngineThreadCell::PublicationTrace)],"shorter values retain actual string capacity");
  r.CpuWitnesses().clear();r.GpuFrames.clear();bodies.clear();record(7);
  out.final=snap();out.query_unchanged=snap().requests==out.final.requests;require(out.query_unchanged,"query retains source state");return out;
}
inline void PopulateSmallEngineThreadStorage(const std::string &text){
  auto &r=detail::CallingRenderThreadState();r.CpuWitnesses()[{0,0,0,0,BlockId{1}}].second=std::make_shared<BoundSectionLightCapture>();
  r.PreviewStates()[{0,0,0}].shown=true;r.GpuFrames.resize(1);
  r.Environment().CelestialBodies.resize(1);r.Environment().CelestialBodies[0].Id=text;
  detail::CallingMeshPublicationThreadState().LastState=text;
}
inline nlohmann::json RunEngineThreadStorageProbe(){
  const std::string text(8192,'q');EngineThreadExercise result;bool controls{},binding_roots{};std::exception_ptr error;
  std::thread owner([&]{try{
    result=ExerciseEngineThreadStorage(text,[](const auto&){});
    struct Roots {
      std::array<const std::shared_ptr<SectionRenderLightBinding>*,3> actual{};size_t calls{};
      void Binding(const std::shared_ptr<SectionRenderLightBinding> &v){if(calls<actual.size())actual[calls]=&v;++calls;}
    } roots;
    {auto &cpu=detail::CallingRenderThreadState().CpuWitnesses();
      for(int i=0;i<3;++i)cpu[{i,0,0,0,BlockId{1}}];
      InspectCallingThreadRenderWitnessRoots(roots);binding_roots=roots.calls==3;
      size_t i=0;for(const auto &entry:cpu){binding_roots=binding_roots && roots.actual[i++]==&entry.second.first;}
      cpu.clear();}
    AllocatorSourcePhase phase(12001,1);EngineThreadStoragePacket packet;ExportEngineThreadStorage(packet,phase,0,true,true);
    std::vector<const EngineThreadStoragePacket*> packets{&packet};
    const auto accepted=JoinEngineThreadStorage(phase,packets,true,true);
    auto wrong=packet;wrong.epoch++;std::vector<const EngineThreadStoragePacket*> stale{&wrong};
    auto alias=packet;std::vector<const EngineThreadStoragePacket*> aliases(64,&alias);
    const auto joined_alias=JoinEngineThreadStorage(phase,aliases,true,true);
    auto changed=packet;changed.source.requests[0]++;std::vector<const EngineThreadStoragePacket*> conflict{&packet,&changed};
    auto missing=packets;missing.clear();auto upper=EngineThreadFixtureUpper();upper.input_history_known=false;
    auto huge=result.final;huge.requests[0]=SIZE_MAX;
    controls=accepted.supported && accepted.requested==result.final.Total() && joined_alias.supported && joined_alias.requested==accepted.requested &&
      !JoinEngineThreadStorage(phase,stale,true,true).supported && !JoinEngineThreadStorage(phase,conflict,true,true).supported &&
      !JoinEngineThreadStorage(phase,missing,true,true).supported && !JoinEngineThreadStorage(phase,packets,false,true).supported &&
      !JoinEngineThreadStorage(phase,packets,true,false).supported && !InspectCallingThreadEngineStorage(false,true).supported &&
      !InspectCallingThreadEngineStorage(true,false).supported && !BoundEngineThreadTransition(result.final,upper).supported &&
      BoundEngineThreadTransition(huge,EngineThreadFixtureUpper()).overflow;
    auto profile=packet;profile.source.layout.widths[0]++;std::vector<const EngineThreadStoragePacket*> mixed{&packet,&profile};
    controls=controls && !JoinEngineThreadStorage(phase,mixed,true,true).supported;
    profile=packet;profile.source.layout.supported=false;std::vector<const EngineThreadStoragePacket*> invalid_profile{&profile};
    controls=controls && !JoinEngineThreadStorage(phase,invalid_profile,true,true).supported;
    profile=packet;profile.source.cells[0]=0;
    controls=controls && !JoinEngineThreadStorage(phase,invalid_profile,true,true).supported;
    phase.Close();controls=controls && !JoinEngineThreadStorage(phase,packets,true,true).supported;
  }catch(...){error=std::current_exception();}});owner.join();if(error)std::rethrow_exception(error);
  if(!controls || !binding_roots)throw std::runtime_error("engine source phase/history/alias/coverage controls");
  return {{"schema","engine-thread-storage.v1"},{"status","PASS"},{"inline_bytes",result.final.layout.inline_bytes},{"widths",result.final.layout.widths},
    {"large_threshold",result.final.layout.threshold},{"large_slack",result.final.layout.slack},{"retained",result.retained},{"final_requests",result.final.requests},
    {"checks",result.checks},{"fresh_no_backing",result.fresh},{"clear_retains_capacity",result.capacity_retained},{"query_unchanged",result.query_unchanged},
    {"borrowed_binding_roots",binding_roots},{"history_phase_alias_coverage_controls",controls},{"scope","Actual owning-thread render/world/mesh/item/light TLS parents and default requested backing,including source-owned capture coallocations. Supplied stable factory/binding graph only;foreign threads,external item collectors,window/OS/CRT/GPU/whole process remain separate."}};
}
} // namespace cutum
