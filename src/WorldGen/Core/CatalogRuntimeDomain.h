#pragma once
#include "WorldGen/Core/CatalogPublicationRoots.h"
#include "Core/Jobs/JobThreadPool.h"
#include <string>

namespace cutum
{
// Synchronous supplied-owner census. This object owns only weak catalog
// controls and diagnostic scratch; all application owners must remain stable.
// It does not inspect foreign-thread TLS, enumerate OS threads, or measure RSS.
class CatalogRuntimeDomain:public CatalogPublicationRootInspector
{
public:
  enum class Kind {Core,Application,World,Geometry,GuiIcon,ObjectIcon,Preview,
    Viewmodel,ItemLab,ItemLabScene,MeshService,MeshCache,Streaming,Persistence,Io,Coop,CallerText,CallingThread,ActiveDirectory,Count};
  struct PoolRow {const void *identity{};std::string kind;JobThreadPoolSnapshot state;size_t worker_cap{};};
  struct BlockRole {std::string role;const void *storage{};};
  std::array<size_t,static_cast<size_t>(Kind::Count)> objects{};
  std::vector<PoolRow> pools;
  std::vector<BlockRole> block_roles;
  size_t workers{},unknown_owners{},biome_aliases{},builtin_aliases{},main_threads{1};
  bool overflow{false};
  CatalogStorageCensus Catalogs()const{return Storage.Result;}
  bool Supported()const{return !overflow && !unknown_owners && !Counts.overflow && !Storage.Result.overflow;}
  bool Enter(Kind kind,const void *address){
    if(!address)return false;
    for(const auto &seen:Seen)if(seen.kind==kind && seen.address==address)return false;
    Seen.push_back({kind,address});Add(objects[static_cast<size_t>(kind)],1);return true;
  }
  void Unknown(){Add(unknown_owners,1);}
  size_t CallingAndPoolThreadUpper(){size_t count=main_threads;Add(count,workers);return count;}
  void BlockStorage(const std::shared_ptr<const UBlockDefinitionStorage> &v,const char *role=nullptr){
    if(v && role)block_roles.push_back({role,v.get()});Storage.BlockStorage(v);
  }
  // Shared weak-only storage with publication roots;aliases crossing resource,
  // field,job result and backend boundaries must not be added as separate sums.
  void CallerText(const std::string &v){if(Enter(Kind::CallerText,&v))Storage.Text(v);}
  void Library(const std::shared_ptr<const UObjectLibrary> &v){Storage.Library(v);}
  void Merge(const std::shared_ptr<const UBlockMergeRegistry> &v){Storage.Merge(v);}
  void Placeholder(const std::shared_ptr<const UPlaceholderTextureCache> &v){Storage.Placeholder(v);}
  void TextureBases(const std::shared_ptr<const UTextureBaseStorage> &v){Storage.TextureBases(v);}
  void TextureCubes(const std::shared_ptr<const UTextureCubeStorage> &v){Storage.TextureCubes(v);}
  void Registry(const UBlockRegistry *v){Storage.Registry(v);}
  void Content(const WorldGenContentSnapshot &v){Storage.Snapshot(v);}
  void FeaturesValue(const ObjectFeatureConfig &v){
    // A world-owned value, not a new shared allocation or owner.
    Storage.Features(std::shared_ptr<const ObjectFeatureConfig>(std::shared_ptr<const ObjectFeatureConfig>{},&v));
  }
  void CallingThread(){if(Enter(Kind::CallingThread,&UBlockRegistry::GetThreadNameForCensus()))Storage.CallingThreadReads();}
  void ActiveDirectory(){if(Enter(Kind::ActiveDirectory,&UWorldGenPack::GetActiveDirectoryForCensus())){CatalogResourceRoots roots;roots.active_pack_directory=true;Storage.Resources(roots);}}
  void Pool(const char *kind,const UJobThreadPool &pool,size_t cap){PoolSnapshot(kind,&pool,pool.GetSnapshot(),cap);}
  void PoolSnapshot(const char *kind,const void *identity,JobThreadPoolSnapshot state,size_t cap){
    if(!identity)return;
    for(const auto &row:pools)if(row.identity==identity){if(row.kind!=kind || row.worker_cap!=cap)Unknown();return;}
    pools.push_back({identity,kind,state,cap});
    if(!cap || state.workers>cap || state.active>state.workers)Unknown();
    Add(workers,state.workers);
  }
  // Registry candidates are weak and supplied by the caller. The alias alone
  // cannot safely reveal the type/address of an arbitrary shared owner.
  void BiomeAlias(const BiomeKeyRef &key,
      const std::vector<std::weak_ptr<const UBiomeDefinitionRegistry>> &candidates,
      const std::vector<const BiomeKey *> &builtins={}){
    const auto &alias=key.OwnerForCensus();if(!alias)return;
    if(!alias.use_count()){
      for(const auto *value:builtins)if(value==alias.get()){Add(builtin_aliases,1);return;}
      Unknown();return;
    }
    for(const auto &weak:candidates)if(auto registry=weak.lock()){
      if(alias.owner_before(registry) || registry.owner_before(alias))continue;
      const auto handle=registry->Find(*alias);
      if(registry->Key(handle)!=alias.get()){Unknown();return;}
      Add(biome_aliases,1);Storage.Biomes(registry);return;
    }
    Unknown();
  }
private:
  struct SeenObject {Kind kind;const void *address;};
  std::vector<SeenObject> Seen;
  void Add(size_t &n,size_t value){if(value>SIZE_MAX-n){n=SIZE_MAX;overflow=true;}else n+=value;}
};
} // namespace cutum
