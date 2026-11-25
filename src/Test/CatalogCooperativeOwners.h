#pragma once
#include "Test/CatalogQueueOwnersCommon.h"
#include "WorldGen/Core/CooperativePopulateDispatch.h"

static nlohmann::json TestCatalogCooperativeOwners()
{
  using namespace catalog_queue_test;
  struct Payload {
    std::weak_ptr<const WorldGenPack> pack;
    std::weak_ptr<const ObjectFeatureConfig> features;
    std::weak_ptr<const WorldGenRefsCatalog> refs;
    std::weak_ptr<const UBiomeDefinitionRegistry> biomes;
    bool Live()const{return !pack.expired() && !features.expired() && !refs.expired() && !biomes.expired();}
    bool Empty()const{return pack.expired() && features.expired() && refs.expired() && biomes.expired();}
  };
  struct Populator final:IUChunkPopulator {
    std::atomic<size_t> calls{0};
    ChunkPopulateResult Populate(const ChunkPopulateRequest &request)override {
      UWorldGenContentPinScope pin(request.content);
      Require(GetPinnedWorldGenContent()==&pin.Get() && pin.Get().Pack && pin.Get().Biomes && pin.Get().Features && pin.Get().Refs,"real TLS pin covers dispatch");
      ++calls;ChunkPopulateResult result;result.coord=request.chunkCoord;result.token=request.token;return result;
    }
  } populator;
  UCompletedJobQueue<ChunkPopulateResult> completed;
  UJobThreadPool pool(4,"CatalogCooperativeDispatch");
  std::array<Gate,4> gates;
  for(auto &gate:gates)pool.Enqueue([&gate]{gate.Pause();});
  for(auto &gate:gates)gate.Wait();
  for(int i=0;i<8;++i)Require(UPipelineAdmission::Get().TryAcquireWorkSlot(),"hold unrelated global work slots");
  Require(!UPipelineAdmission::Get().TryAcquireWorkSlot(),"global work limit reached");
  const auto enqueue=[&](int i){
    ChunkPopulateRequest request;request.chunkCoord={i,0,0};
    request.content={std::make_shared<WorldGenPack>(),std::make_shared<UBiomeDefinitionRegistry>(),std::make_shared<ObjectFeatureConfig>(),std::make_shared<WorldGenRefsCatalog>()};
    Payload weak{request.content.Pack,request.content.Features,request.content.Refs,request.content.Biomes};
    EnqueueCooperativePopulate(pool,&populator,request,&completed);return weak;
  };
  std::vector<Payload> old;for(int i=0;i<8;++i)old.push_back(enqueue(i));
  for(const auto &p:old)Require(p.Live(),"cooperative queued content owned independently of shared global work cap");
  for(auto &gate:gates)gate.Release();Require(pool.WaitIdleFor(std::chrono::seconds(10)),"cooperative complete idle");
  Require(populator.calls==8 && completed.Size()==8,"all real dispatches completed");
  for(const auto &p:old)Require(p.Empty(),"completed populate buffers hold no content catalogs");
  auto returned=completed.DrainAll();Require(returned.size()==8,"cooperative caller receives eight results");
  std::set<int> coords;for(const auto &r:returned)coords.insert(r.coord.x);Require(coords.size()==8 && *coords.begin()==0 && *coords.rbegin()==7,"request result coordinates preserved");
  for(int i=0;i<8;++i)UPipelineAdmission::Get().ReleaseWorkSlot();
  Gate cancellation;pool.Enqueue([&]{cancellation.Pause();});cancellation.Wait();
  // Park all workers before adding the cancel cohort.
  std::array<Gate,3> peers;for(auto &gate:peers)pool.Enqueue([&gate]{gate.Pause();});for(auto &gate:peers)gate.Wait();
  old.clear();for(int i=0;i<8;++i)old.push_back(enqueue(i));pool.CancelPendingJobs();
  for(const auto &p:old)Require(p.Empty(),"cooperative queued cancellation retires content");
  cancellation.Release();for(auto &gate:peers)gate.Release();Require(pool.WaitIdleFor(std::chrono::seconds(10)),"cooperative cancelled idle");
  Require(populator.calls==8,"cancel cohort never populated");
  return {{"schema","catalog-cooperative-dispatch-owners.v1"},{"status","PASS"},{"workers",4},{"queued_requests",8},{"content_roots_per_request",4},{"unrelated_global_slots_held",8},{"completed_results",8},{"completed_catalog_payloads",0},{"cancelled_payloads",0},{"scope","Production dispatch callable and pool,deterministic populator using actual TLS pin;not full procedural pipeline or cooperative scheduling acceptance"}};
}
