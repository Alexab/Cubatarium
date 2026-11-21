#pragma once
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/RuntimeSettingsText.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Objects/ObjectLibrary.h"
#include <array>
#include <limits>
#include <list>

namespace cutum
{
enum class CatalogObjectKind {Pack,Biomes,Features,Refs,Image,Diagnostics,Blocks,PrefabIndex,Prefab,Count};
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
inline constexpr bool kCatalogMsvcUnorderedModel=true;
#else
inline constexpr bool kCatalogMsvcUnorderedModel=false;
#endif

// Live supplied roots only. Known storage includes object shells, vector
// capacity, map values, external string capacity and diagnostic arrays.
// Map links/buckets, shared control storage and allocator/platform overhead
// are separate implementation costs, NOT covered by known_storage_bytes.
struct CatalogStorageCensus
{
  size_t known_storage_bytes{0},object_bytes{0},vector_bytes{0},map_value_bytes{0};
  size_t text_bytes{0},diagnostic_bytes{0},vectors{0},tables{0},nodes{0},bucket_slots{0},strings{0};
  size_t shared_owners{0},input_snapshot_handle_bytes{0},vector_allocations{0},msvc_unordered_extra_bytes{0};
  std::array<size_t,static_cast<size_t>(CatalogObjectKind::Count)> objects{};
  bool overflow{false};
  bool unordered_model_supported{kCatalogMsvcUnorderedModel};
  size_t Objects(CatalogObjectKind kind) const {return objects[static_cast<size_t>(kind)];}
};

namespace detail
{
class CatalogStorageInspector
{
  struct SeenObject {CatalogObjectKind kind;const void *address;};
  std::vector<SeenObject> Seen;
  std::vector<std::weak_ptr<const void>> Owners;
  void Add(size_t &total,size_t value) {
    if(value>std::numeric_limits<size_t>::max()-total) {Result.overflow=true;total=std::numeric_limits<size_t>::max();}
    else total+=value;
  }
  size_t Multiply(size_t count,size_t width) {
    if(width && count>std::numeric_limits<size_t>::max()/width) {Result.overflow=true;return std::numeric_limits<size_t>::max();}
    return count*width;
  }
  void Bytes(size_t &column,size_t count,size_t width=1) {
    const auto bytes=Multiply(count,width);Add(column,bytes);Add(Result.known_storage_bytes,bytes);
  }
  template<class T> bool Shared(CatalogObjectKind kind,const std::shared_ptr<T> &value) {
    if(!value) return false;
    for(const auto &seen:Seen) if(seen.kind==kind && seen.address==value.get()) return false;
    Seen.push_back({kind,value.get()});
    std::shared_ptr<const void> owner=value;
    bool owner_seen=owner.use_count()==0;
    for(const auto &previous:Owners) if(!previous.owner_before(owner) && !owner.owner_before(previous)) {owner_seen=true;break;}
    if(!owner_seen) {Owners.push_back(owner);++Result.shared_owners;}
    ++Result.objects[static_cast<size_t>(kind)];Bytes(Result.object_bytes,1,sizeof(T));
    return true;
  }
  void Rule(const ObjectFeatureRule &rule) {
    Text(rule.ObjectName);Text(rule.Scatter.BlockName);Text(rule.Tree.TrunkBlock);Text(rule.Tree.LeavesBlock);
    Text(rule.Shrub.StemBlock);Text(rule.Shrub.LeavesBlock);Text(rule.Shrub.AccentBlock);
    Vector(rule.Biomes);Vector(rule.BiomeKeys);Vector(rule.SubBiomes);
    for(const auto &key:rule.BiomeKeys) Text(key);
  }
  void Definition(const BlockDefinition &definition) {
    Text(definition.Name);Text(definition.DisplayName);Vector(definition.Types);Map(definition.DigGroups);
    for(const auto &type:definition.Types) Text(type);
    for(const auto &entry:definition.DigGroups) Text(entry.first);
  }
public:
  CatalogStorageCensus Result;
  void Text(const std::string &text) {++Result.strings;Bytes(Result.text_bytes,ExternalStringCapacityBytes(text));}
  template<class T,class A> void Vector(const std::vector<T,A> &values) {
    ++Result.vectors;if(values.capacity()) ++Result.vector_allocations;
    Bytes(Result.vector_bytes,values.capacity(),sizeof(T));
  }
  template<class MapType> void Map(const MapType &values) {
    ++Result.tables;Add(Result.nodes,values.size());Add(Result.bucket_slots,values.bucket_count());
    Bytes(Result.map_value_bytes,values.size(),sizeof(typename MapType::value_type));
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    // Current MSVC xhash: one list sentinel, one node per value and two
    // unchecked iterators per bucket. _Hash_vec capacity equals its size.
    // Validate against the actual STL node type, not a guessed link size.
    using Value=typename MapType::value_type;
    using VoidPointer=typename std::allocator_traits<typename MapType::allocator_type>::void_pointer;
    static_assert(std::is_same_v<VoidPointer,void*> && sizeof(typename MapType::iterator)==sizeof(void*));
    using Node=std::_List_node<Value,VoidPointer>;
    Add(Result.msvc_unordered_extra_bytes,Multiply(values.size(),sizeof(Node)-sizeof(Value)));
    Add(Result.msvc_unordered_extra_bytes,sizeof(Node));
    Add(Result.msvc_unordered_extra_bytes,Multiply(values.bucket_count(),2*sizeof(typename MapType::iterator)));
#endif
  }
  void Biomes(const std::shared_ptr<const UBiomeDefinitionRegistry> &registry) {
    if(Shared(CatalogObjectKind::Biomes,registry)) registry->InspectStorage(*this);
  }
  void Pack(const std::shared_ptr<const WorldGenPack> &pack) {
    if(!Shared(CatalogObjectKind::Pack,pack)) return;
    Text(pack->Id);Text(pack->BiomeMapImagePath);Text(pack->Ravines.FeatherMode);
    Vector(pack->Pipeline.StageOrder);Vector(pack->Ores.Rules);
    for(const auto &ore:pack->Ores.Rules) Text(ore.Slot);
    Vector(pack->SurfaceClimateBiomes);Vector(pack->CaveClimateBiomes);Map(pack->Biomes);
    for(const auto &entry:pack->Biomes) {
      Text(entry.first);const auto &biome=entry.second;
      Text(biome.SurfaceSlot);Text(biome.SubsurfaceSlot);Text(biome.SurfaceSlotV3);Text(biome.SubsurfaceSlotV3);
      Map(biome.FeatureWeights);for(const auto &weight:biome.FeatureWeights) Text(weight.first);
      Map(biome.SubBiomes);for(const auto &sub:biome.SubBiomes) {Text(sub.first);Text(sub.second.SubsurfaceSlot);}
    }
    Biomes(pack->BiomeRegistry);
    if(Shared(CatalogObjectKind::Image,pack->BiomeMapImage)) Vector(pack->BiomeMapImage->Pixels);
  }
  void Features(const std::shared_ptr<const ObjectFeatureConfig> &features) {
    if(!Shared(CatalogObjectKind::Features,features)) return;
    for(const auto *pool:{&features->Vegetation,&features->GroundCover,&features->Decoration,&features->Structures}) {
      Vector(*pool);for(const auto &rule:*pool) Rule(rule);
    }
    Vector(features->CaveFeatures);
    for(const auto &rule:features->CaveFeatures) {Text(rule.BiomeKeyName);Text(rule.BlockName);}
    const auto diagnostics=features->Diagnostics.Snapshot();
    if(Shared(CatalogObjectKind::Diagnostics,diagnostics)) {
      Result.vectors+=3;if(diagnostics->RuleCount()) Result.vector_allocations+=3;
      Bytes(Result.diagnostic_bytes,diagnostics->CapacityBytes());
    }
  }
  void Refs(const std::shared_ptr<const WorldGenRefsCatalog> &refs) {
    if(!Shared(CatalogObjectKind::Refs,refs)) return;
    Map(*refs);
    for(const auto &entry:*refs) {
      Text(entry.first);Text(entry.second.FallbackSlot);Vector(entry.second.BlockNames);
      for(const auto &name:entry.second.BlockNames) Text(name);
    }
  }
  void Blocks(const std::shared_ptr<const BlockDefinitionCatalog> &blocks) {
    if(!Shared(CatalogObjectKind::Blocks,blocks)) return;
    Map(blocks->ById);Map(blocks->NameToId);
    for(const auto &entry:blocks->ById) Definition(entry.second);
    for(const auto &entry:blocks->NameToId) Text(entry.first);
  }
  template<class T> void Prefab(const std::shared_ptr<T> &prefab) {
    if(!Shared(CatalogObjectKind::Prefab,prefab)) return;
    Text(prefab->Name);Text(prefab->DisplayName);Text(prefab->SourcePackId);Vector(prefab->Tags);Vector(prefab->voxels);
    for(const auto &tag:prefab->Tags) Text(tag);
    for(const auto &voxel:prefab->voxels) Text(voxel.Type);
  }
  void Library(const std::shared_ptr<const UObjectLibrary> &library) {
    if(Shared(CatalogObjectKind::PrefabIndex,library)) library->InspectStorage(*this);
  }
  void Snapshot(const WorldGenContentSnapshot &snapshot) {Pack(snapshot.Pack);Biomes(snapshot.Biomes);Features(snapshot.Features);Refs(snapshot.Refs);}
  void HandleBytes(size_t count) {Result.input_snapshot_handle_bytes=Multiply(count,sizeof(WorldGenContentSnapshot));}
};
} // namespace detail

// All supplied root owners must remain live for this synchronous census.
// Catalog load/rebind must be quiescent. Collector scratch is temporary,
// non-owning, and is not included in the observed catalog storage totals.
inline CatalogStorageCensus MeasureCatalogStorage(
    const WorldGenContentSnapshot *snapshots,size_t snapshot_count,
    const std::shared_ptr<const BlockDefinitionCatalog> *blocks=nullptr,size_t block_count=0,
    const std::shared_ptr<const UObjectLibrary> &library={})
{
  detail::CatalogStorageInspector out;out.HandleBytes(snapshot_count);
  for(size_t n=0;n<snapshot_count;++n) out.Snapshot(snapshots[n]);
  for(size_t n=0;n<block_count;++n) out.Blocks(blocks[n]);
  out.Library(library);return out.Result;
}
} // namespace cutum
