#pragma once
#include "WorldGen/Core/CatalogPublicationRoots.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <fstream>

namespace catalog_queue_test
{
using namespace cutum;
using Json=nlohmann::json;
inline void Require(bool value,const char *message)
{if(!value){std::cerr<<"catalog_queue_owners: "<<message<<'\n';std::exit(1);}}
struct Gate
{
  std::mutex mutex;std::condition_variable cv;bool entered{},released{};
  void Pause(){std::unique_lock lock(mutex);entered=true;cv.notify_all();Require(cv.wait_for(lock,std::chrono::seconds(10),[&]{return released;}),"worker gate deadline");}
  void Wait(){std::unique_lock lock(mutex);Require(cv.wait_for(lock,std::chrono::seconds(10),[&]{return entered;}),"worker entered deadline");}
  void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
inline std::weak_ptr<const BlockDefinitionCatalog> Publish(const std::shared_ptr<UBlockDefinitionStorage> &storage,int version)
{
  BlockDefinition stone;stone.Name="stone";stone.DisplayName=std::string(4096,char('a'+version%26));stone.Physics=BlockPhysicsProfile::Solid();
  storage->ReplaceAll({{8,stone}},{{stone.Name,8}});return storage->GetCatalogSnapshot();
}
inline size_t Live(const std::vector<std::weak_ptr<const BlockDefinitionCatalog>> &values)
{size_t count{};for(const auto &v:values)count+=!v.expired();return count;}
inline void ReleaseReads(){UBlockDefinitionStorage::ReleaseThreadReads();UBlockRegistry::ReleaseThreadName();}
inline void Scene(UBlockWorld &world,const UBlockDefinitionStorage *definitions,int columns=1)
{
  world.SetFluidDefinitions(definitions);world.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
  for(int i=0;i<columns;++i)world.SetBlock({i*CHUNK_SIZE+4,4,4},8);
}
template<class Builder,class Submit> Json CompletedOwners(Builder &builder,
    const std::shared_ptr<UBlockDefinitionStorage> &definitions,Submit submit)
{
  std::vector<std::weak_ptr<const BlockDefinitionCatalog>> old;
  builder.SetCompletedCapacity(2);
  for(int i=0;i<3;++i){old.push_back(Publish(definitions,30+i));submit(i);Require(builder.WaitIdleFor(std::chrono::seconds(10)),"completed idle");}
  Publish(definitions,39);ReleaseReads();
  Require(old[0].expired() && Live(old)==2 && builder.GetCompletedSize()==2,"overflow retires oldest catalog only");
  CatalogPublicationRootInspector completed;builder.InspectCompletedCatalogRoots(completed);
  Require(completed.Catalogs().Objects(CatalogObjectKind::Blocks)==2,"actual completed queue union");
  builder.SetCompletedCapacity(1);
  Require(old[1].expired() && !old[2].expired(),"capacity shrink retires removed catalog");
  auto returned=builder.DrainCompleted(8);
  Require(returned.size()==1 && builder.GetCompletedSize()==0 && !old[2].expired(),"drain transfers ownership to caller");
  returned.clear();Require(Live(old)==0,"caller release retires payload despite inspector");
  old.clear();builder.SetCompletedCapacity(0);
  for(int i=0;i<2;++i){old.push_back(Publish(definitions,40+i));submit(4+i);Require(builder.WaitIdleFor(std::chrono::seconds(10)),"uncapped idle");}
  Publish(definitions,49);ReleaseReads();
  CatalogPublicationRootInspector uncapped;builder.InspectCompletedCatalogRoots(uncapped);
  Require(builder.GetCompletedCapacity()==0 && builder.GetCompletedSize()==2 && Live(old)==2 && uncapped.Catalogs().Objects(CatalogObjectKind::Blocks)==2,"zero capacity is lossless unbounded storage");
  auto uncapped_results=builder.DrainCompleted(8);Require(uncapped_results.size()==2 && Live(old)==2,"uncapped drain transfers roots");
  uncapped_results.clear();Require(Live(old)==0,"uncapped caller release retires payloads");
  return {{"overflow_catalogs",2},{"shrink_catalogs",1},{"drained_caller_catalogs",1},{"after_caller_release",0},{"capacity_zero_retained",2},{"weak_inspectors_do_not_retain_payload",true}};
}
inline void WriteReport(const char *path,const Json &report)
{std::ofstream out(path);out<<report.dump(2)<<'\n';Require(bool(out),"report write");std::cout<<report.dump()<<'\n';}
} // namespace catalog_queue_test
