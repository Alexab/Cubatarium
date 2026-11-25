#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/FluidTestHelpers.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "WorldGen/Core/CatalogThreadReads.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

// Ordinary-new refusal is thread-local. Concurrent worker setup is never
// charged to a single-thread observer or rejected by another thread's test.
namespace allocation_probe {thread_local bool refuse{};thread_local size_t attempts{};}
void *operator new(size_t n){if(allocation_probe::refuse){++allocation_probe::attempts;throw std::bad_alloc{};}if(auto *p=std::malloc(std::max(n,size_t{1})))return p;throw std::bad_alloc{};}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,size_t)noexcept{std::free(p);}
void operator delete[](void *p,size_t)noexcept{std::free(p);}
void *operator new(size_t n,const std::nothrow_t &)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t &)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t &)noexcept{std::free(p);}
void operator delete[](void *p,const std::nothrow_t &)noexcept{std::free(p);}

namespace cutum
{
struct CatalogWorkerRetirementProbe
{
  static bool Wait(UChunkLoadScheduler &scheduler)
  {return scheduler.Pool.WaitIdleFor(std::chrono::seconds(10));}
};
}

namespace
{
using namespace cutum;
using Json=nlohmann::json;
namespace fs=std::filesystem;
constexpr const char *Name="catalog_worker_retirement_test";
constexpr auto Deadline=std::chrono::seconds(10);
void Check(bool value,const char *message){FluidTest::Expect(value,Name,message);}
void Write(const fs::path &path,const std::string &value)
{std::ofstream out(path);out<<value;Check(bool(out),"fixture write");}
void Publish(const fs::path &root,int version)
{
  Write(root/"pack.json","{\"id\":\"retirement_"+std::to_string(version)+"\"}");
  Write(root/"features.json","{}");
  Write(root/"refs.json",R"({"slots":{"stone":{"block_names":["stone"]}}})");
  Check(UWorldGenPack::LoadFromDirectory(root.string()),"real pack publication");
  Check(UObjectFeatureConfigStorage::LoadFromFile(root/"features.json"),"real features publication");
  Check(UWorldGenRefs::LoadFromFile(root/"refs.json"),"real refs publication");
}
struct Probe
{
  std::weak_ptr<const WorldGenPack> pack;
  std::weak_ptr<const ObjectFeatureConfig> features;
  std::weak_ptr<const WorldGenRefsCatalog> refs;
  std::array<std::weak_ptr<const BlockDefinitionCatalog>,3> blocks;
  std::weak_ptr<const WorldObjectDefinition> prefab;
  size_t name_bytes{};
  std::thread::id thread;
  size_t Live()const
  {size_t n=!pack.expired()+!features.expired()+!refs.expired()+!prefab.expired();for(const auto &p:blocks)n+=!p.expired();return n;}
};
size_t ReaderCount()
{
  size_t n=bool(UWorldGenPack::GetThreadReadSnapshot())+bool(UObjectFeatureConfigStorage::GetThreadReadSnapshot())+
      bool(UWorldGenRefs::GetThreadReadSnapshot())+bool(UObjectLibrary::GetThreadReadSnapshot());
  for(const auto &p:UBlockDefinitionStorage::GetThreadReadSnapshots())n+=bool(p);return n;
}
bool Empty(){return ReaderCount()==0 && ExternalStringCapacityBytes(UBlockRegistry::GetThreadNameForCensus())==0;}
// All access is by the owning worker. Main receives only weak probes after a
// mutex/CV barrier; there is no foreign-thread TLS traversal.
Probe Fill(const fs::path &objects)
{
  Probe p;p.thread=std::this_thread::get_id();
  (void)UWorldGenPack::Get();(void)UObjectFeatureConfigStorage::Get();(void)UWorldGenRefs::GetSlot("stone");
  p.pack=UWorldGenPack::GetThreadReadSnapshot();p.features=UObjectFeatureConfigStorage::GetThreadReadSnapshot();p.refs=UWorldGenRefs::GetThreadReadSnapshot();
  for(int i=0;i<3;++i){
    auto storage=std::make_shared<UBlockDefinitionStorage>();
    BlockDefinition d;d.Name=i==0?std::string(64*1024,'n'):"stone";d.Physics=BlockPhysicsProfile::Solid();
    storage->ReplaceAll({{8,d}},{{d.Name,8}});p.blocks[i]=storage->GetCatalogSnapshot();
    if(i==0){(void)storage->GetById(8);UBlockRegistry registry(nullptr,storage);(void)registry.GetTypeNameById(8);}
    if(i==1)(void)storage->GetByName(d.Name);
    if(i==2){
      (void)storage->GetByIdFast(8);UBlockRegistry registry(nullptr,storage);
      UObjectLibrary library;library.LoadMerged(objects,{},registry);
      Check(library.Get("probe")!=nullptr,"real prefab convenience read");p.prefab=UObjectLibrary::GetThreadReadSnapshot();
    }
  }
  p.name_bytes=ExternalStringCapacityBytes(UBlockRegistry::GetThreadNameForCensus());
  Check(p.Live()==7 && p.name_bytes>=64*1024+1,"all seven distinct convenience roots and copied name filled");
  return p;
}
class Populator final:public IUChunkPopulator
{
public:
  fs::path objects;
  std::mutex mutex;std::condition_variable cv;
  std::array<Probe,4> probes{};
  size_t entered{},finished{},cancelled{},dirty_entries{};bool release{},budget{},discard{};
  ChunkPopulateResult Populate(const ChunkPopulateRequest &request)override
  {
    const bool clean=Empty();auto p=Fill(objects);
    {
      std::unique_lock lock(mutex);Check(entered<probes.size(),"bounded host worker observations");
      if(!clean)++dirty_entries;
      probes[entered++]=std::move(p);cv.notify_all();
      Check(cv.wait_for(lock,Deadline,[&]{return release;}),"worker gate deadline");
      if(request.shouldCancel && request.shouldCancel())++cancelled;
      ++finished;
    }
    if(budget)throw UBufferBudgetExceeded{};
    ChunkPopulateResult result;result.coord=request.chunkCoord;result.token=request.token;result.discarded=discard;return result;
  }
  void Wait(size_t count)
  {std::unique_lock lock(mutex);Check(cv.wait_for(lock,Deadline,[&]{return entered==count;}),"all host workers reached gate");}
  void Release(){std::lock_guard lock(mutex);release=true;cv.notify_all();}
  void Reset(){std::lock_guard lock(mutex);entered=finished=cancelled=dirty_entries=0;release=false;probes={};}
};
Json Scenario(const fs::path &root,int version,bool baseline,bool cancel,bool budget)
{
  Publish(root,version);
  Populator populator;populator.objects=root/"objects";populator.budget=budget;
  UChunkGenerationRegistry tokens;UChunkLoadScheduler scheduler(populator,tokens);
  const auto workers=ComputeWorkerThreadCount(JobPoolKind::ChunkGeneration);
  Check(workers>=1 && workers<=4,"declared host generation worker bound");
  ProceduralSettings settings;settings.MaxHeight=16;
  for(size_t i=0;i<workers;++i)Check(scheduler.RequestLoad({int(i),0,version},int(i),settings),"scheduler accepts distinct request");
  Check(scheduler.StartQueuedGenerationWorkers(int(workers))==workers,"all host workers scheduled");
  populator.Wait(workers);
  for(size_t i=0;i<workers;++i)for(size_t j=0;j<i;++j)Check(populator.probes[i].thread!=populator.probes[j].thread,"barrier occupies different actual workers");
  Publish(root,version+1);
  for(size_t i=0;i<workers;++i)Check(populator.probes[i].Live()==7,"old payload alive during task after replacement");
  if(cancel)for(size_t i=0;i<workers;++i)scheduler.Cancel({int(i),0,version});
  populator.Release();Check(CatalogWorkerRetirementProbe::Wait(scheduler),"scheduler idle includes callable retirement");
  size_t live{};for(size_t i=0;i<workers;++i)live+=populator.probes[i].Live();
  Check(live==(baseline?7*workers:0),baseline?"baseline exposes seven idle reader roots per worker":"idle workers retain no old convenience payload");
  Check(populator.finished==workers && populator.cancelled==(cancel?workers:0),"normal/token cancellation outcomes observed");
  Check(UPipelineAdmission::Get().WorkSlotsInUse()==0,"all captured work slots retired at idle");
  Json result={{"workers",workers},{"live_old_reader_slots_after_idle",live},{"filled_slots",7*workers},{"name_bytes_per_worker",populator.probes[0].name_bytes},{"cancel",cancel},{"budget_refusal",budget}};
  if(!baseline){
    Check(populator.dirty_entries==0,"fresh workers enter with empty readers/name");
    UBlockWorld world;scheduler.Tick(world,int(workers),0);
    if(budget){
      Check(scheduler.GetPendingQueueCount()==workers,"caught budget refusal preserves all exact retry requests");
      populator.budget=false;
      for(size_t i=0;i<workers;++i){
        populator.Reset();Check(scheduler.StartQueuedGenerationWorkers(1)==1,"actual serial retry admitted");populator.Wait(1);
        Publish(root,version+2+int(i));populator.Release();Check(CatalogWorkerRetirementProbe::Wait(scheduler),"retry idle deadline");
        Check(populator.probes[0].Live()==0 && populator.dirty_entries==0,"retry enters clean and retires all old reader payload");
        scheduler.Tick(world,1,0);
      }
      for(size_t i=0;i<workers;++i)Check(scheduler.IsCommitted({int(i),0,version}),"exact refused coordinate completes on retry");
      result["exact_retries_committed"]=workers;
    }else if(!cancel){
      for(size_t i=0;i<workers;++i)Check(scheduler.IsCommitted({int(i),0,version}),"normal result commits");
      for(int round=0;round<8;++round){
        populator.Reset();populator.discard=round%2!=0;
        const int z=100+round;Publish(root,200+2*round);
        for(size_t i=0;i<workers;++i)Check(scheduler.RequestLoad({int(i),0,z},int(i),settings),"repeated same-pool request");
        Check(scheduler.StartQueuedGenerationWorkers(int(workers))==workers,"repeated full worker overlap");populator.Wait(workers);
        Publish(root,201+2*round);populator.Release();Check(CatalogWorkerRetirementProbe::Wait(scheduler),"repeated idle deadline");
        for(size_t i=0;i<workers;++i)Check(populator.probes[i].Live()==0,"repeated publication retires reader payload");
        Check(populator.dirty_entries==0,"each reused worker has empty slots/name on next task");scheduler.Tick(world,int(workers),0);
        for(size_t i=0;i<workers;++i)Check(scheduler.IsCommitted({int(i),0,z})==!populator.discard,"success/discard terminal state retained");
      }
      result["same_pool_full_overlap_rounds"]=9;result["success_and_discard_terminal_states"]=true;
    }
  }
  scheduler.CancelAllPending(Deadline);Check(scheduler.GetRetainedGenerationResultCount()==0,"completed/retry input owners retired");
  return result;
}
Json ExplicitOwners(const fs::path &root)
{
  Publish(root,500);const auto probe=Fill(root/"objects");
  std::array<std::shared_ptr<const void>,7> owners={UWorldGenPack::GetThreadReadSnapshot(),UObjectFeatureConfigStorage::GetThreadReadSnapshot(),
      UWorldGenRefs::GetThreadReadSnapshot(),UObjectLibrary::GetThreadReadSnapshot()};
  {const auto blocks=UBlockDefinitionStorage::GetThreadReadSnapshots();for(size_t i=0;i<3;++i)owners[4+i]=blocks[i];}
  auto snapshot=CaptureWorldGenContentSnapshot();
  {
    UWorldGenContentPinScope pin(snapshot);Publish(root,501);
    allocation_probe::attempts=0;allocation_probe::refuse=true;ReleaseCallingThreadCatalogReads();ReleaseCallingThreadCatalogReads();allocation_probe::refuse=false;
    Check(allocation_probe::attempts==0 && Empty(),"persistent-refusal release is nonallocating and idempotent");
    Check(GetPinnedWorldGenContent()==&pin.Get() && &UWorldGenPack::Get()==snapshot.Pack.get() &&
        &UObjectFeatureConfigStorage::Get()==snapshot.Features.get() && UWorldGenRefs::GetSlot("stone")==&snapshot.Refs->at("stone"),"release preserves active pin and its exact immutable payload");
    Check(Empty() && probe.Live()==7,"pinned reads do not repopulate convenience slots;explicit snapshots preserve all old payload");
    const auto *prefab=static_cast<const WorldObjectDefinition *>(owners[3].get());
    Check(prefab->voxels.size()==1 && prefab->voxels[0].offset==glm::ivec3(1,2,3) && prefab->voxels[0].Id==8,"escaped prefab retains independent authored values");
  }
  snapshot={};
  std::mutex mutex;std::condition_variable cv;bool ready{},release{};
  std::thread last([held=std::move(owners),&mutex,&cv,&ready,&release]()mutable{
    std::unique_lock lock(mutex);ready=true;cv.notify_one();Check(cv.wait_for(lock,Deadline,[&]{return release;}),"escaped owner gate");
    held={};
  });
  {std::unique_lock lock(mutex);Check(cv.wait_for(lock,Deadline,[&]{return ready;}),"escaped owner received");Check(probe.Live()==7,"last explicit roots transferred to another thread");release=true;cv.notify_one();}
  last.join();Check(probe.Live()==0,"last cross-thread explicit owner retires all payload");
  auto definitions=std::make_shared<UBlockDefinitionStorage>();BlockDefinition block;block.Name="fast_after_release";definitions->ReplaceAll({{9,block}},{{block.Name,9}});
  Check(definitions->GetByIdFast(9)->Name==block.Name,"fast cache first read");ReleaseCallingThreadCatalogReads();
  Check(definitions->GetByIdFast(9)->Name==block.Name,"same-owner same-revision fast cache reloads after release");ReleaseCallingThreadCatalogReads();
  const auto unwind=Fill(root/"objects");Publish(root,502);
  try{UCatalogThreadReadsScope scope;throw 7;}catch(int value){Check(value==7,"standalone caught unwind preserved");}
  Check(Empty() && unwind.Live()==0,"scope unwind releases readers/name");
  bool fresh_clean{};std::thread fresh([&]{allocation_probe::attempts=0;allocation_probe::refuse=true;ReleaseCallingThreadCatalogReads();allocation_probe::refuse=false;fresh_clean=Empty() && allocation_probe::attempts==0;});fresh.join();
  Check(fresh_clean,"fresh-thread release initializes no ordinary-new request");
  return {{"explicit_roots",7},{"last_cross_thread_payloads",0},{"active_pin_preserved",true},{"release_new_attempts",0},{"fast_same_revision_reload",true},{"caught_scope_unwind",true},{"fresh_thread_release",true}};
}
}
int main(int argc,char **argv)
{
  const bool baseline=argc>1 && std::string(argv[1])=="--baseline";
  const auto output=fs::path(argc>(baseline?2:1)?argv[baseline?2:1]:"catalog-worker-retirement.json");
  const auto root=fs::absolute(output.parent_path()/"catalog-worker-retirement-fixture");
  fs::create_directories(root/"objects");
  Write(root/"objects"/"probe.json",R"({"blocks":[{"dx":1,"dy":2,"dz":3,"type":"stone"}]})");
  Json result={{"schema","catalog-worker-retirement.v1"},{"mode",baseline?"baseline":"acceptance"},{"scenarios",Json::array()}};
  result["scenarios"].push_back(Scenario(root,1,baseline,false,false));
  if(!baseline){result["scenarios"].push_back(Scenario(root,3,false,true,false));result["scenarios"].push_back(Scenario(root,5,false,false,true));result["explicit_owners"]=ExplicitOwners(root);}
  result["status"]="PASS";
  std::ofstream out(output);out<<result.dump(2)<<'\n';Check(bool(out),"receipt write");std::cout<<result.dump()<<std::endl;return 0;
}
