#pragma once
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "WorldGen/Core/WorldGenSets.h"
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace cutum
{
struct SourceInputStorageCensus
{
  CatalogStorageCensus storage;
  size_t settings_roots{},json_values{},json_root_references{},settings_inline_bytes{};
  size_t path_roots{},path_inline_bytes{};
  size_t msvc_large_allocation_extra_bytes{};
  bool overflow{};
  // Excludes caller-owned inline roots/reference arrays, collector scratch,
  // allocator/CRT metadata and all external/shared/process/GPU owners.
  size_t RequestedHeapComponents() const {
    if(overflow || storage.overflow) return std::numeric_limits<size_t>::max();
    size_t total=storage.known_storage_bytes;
    for(size_t n:{storage.msvc_unordered_extra_bytes,storage.msvc_tree_extra_bytes,
        storage.msvc_list_extra_bytes,msvc_large_allocation_extra_bytes}) {
      if(n>std::numeric_limits<size_t>::max()-total) return std::numeric_limits<size_t>::max();
      total+=n;
    }return total;
  }
};
namespace detail
{
class SourceInputStorageInspector:public CatalogStorageInspector
{
  std::unordered_set<const WorldGenSets*> SettingsSeen;
  std::unordered_set<const nlohmann::json*> JsonSeen;
  std::unordered_set<const std::filesystem::path*> PathsSeen;
  void Add(size_t &total,size_t value) {
    if(value>std::numeric_limits<size_t>::max()-total) {Extra.overflow=true;total=std::numeric_limits<size_t>::max();}
    else total+=value;
  }
  size_t Multiply(size_t n,size_t width) {
    if(width && n>std::numeric_limits<size_t>::max()/width) {Extra.overflow=true;return std::numeric_limits<size_t>::max();}
    return n*width;
  }
  void HeapObject(size_t bytes) {Add(Result.object_bytes,bytes);Add(Result.known_storage_bytes,bytes);}
  void Alignment(size_t bytes) {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    static_assert(std::_Big_allocation_threshold==4096 && std::_Non_user_size==39);
    if(bytes>=std::_Big_allocation_threshold) Add(Extra.msvc_large_allocation_extra_bytes,std::_Non_user_size);
#endif
  }
public:
  SourceInputStorageCensus Extra;
  void Path(const std::filesystem::path *path) {
    if(!path || !PathsSeen.insert(path).second) return;
#if defined(_MSVC_STL_VERSION)
    static_assert(sizeof(std::filesystem::path)==sizeof(std::filesystem::path::string_type));
#endif
    Add(Extra.path_roots,1);Add(Extra.path_inline_bytes,sizeof(std::filesystem::path));Text(path->native());
  }
  template<class T> void Text(const T &text) {
    CatalogStorageInspector::Text(text);
    const auto address=reinterpret_cast<uintptr_t>(text.data()),object=reinterpret_cast<uintptr_t>(&text);
    if(address<object || address-object>=sizeof(text)) {
      if(text.capacity()==std::numeric_limits<size_t>::max()) Extra.overflow=true;
      else Alignment(Multiply(text.capacity()+1,sizeof(typename T::value_type)));
    }
  }
  template<class T> void Vector(const T &values) {
    CatalogStorageInspector::Vector(values);Alignment(Multiply(values.capacity(),sizeof(typename T::value_type)));
  }
  template<class T> void Map(const T &values) {
    CatalogStorageInspector::Map(values);Alignment(Multiply(values.bucket_count(),2*sizeof(typename T::iterator)));
  }
  void Settings(const WorldGenSets *sets) {
    if(!sets || !SettingsSeen.insert(sets).second) return;
    ++Extra.settings_roots;Add(Extra.settings_inline_bytes,sizeof(WorldGenSets));
    for(const auto *sections:{&sets->VegetationSections,&sets->GroundCoverSections,&sets->DecorationSections,&sets->StructureSections}) {
      Map(*sections);
      for(const auto &section:*sections) {
        Text(section.first);Vector(section.second.Entries);
        for(const auto &entry:section.second.Entries) {
          Text(entry.ObjectName);Text(entry.Scatter.BlockName);Text(entry.Tree.TrunkBlock);Text(entry.Tree.LeavesBlock);
          Text(entry.Shrub.StemBlock);Text(entry.Shrub.LeavesBlock);Text(entry.Shrub.AccentBlock);
          Vector(entry.Biomes);Vector(entry.SubBiomes);
          for(const auto &key:entry.Biomes) Text(key);for(const auto &key:entry.SubBiomes) Text(key);
        }
      }
    }
    Map(sets->Terrain);for(const auto &slot:sets->Terrain) {Text(slot.first);Text(slot.second.Block);}
    Map(sets->Ores);for(const auto &slot:sets->Ores) Text(slot.first);
  }
  void Json(const nlohmann::json *root) {
    if(!root) return;
    ++Extra.json_root_references;
    std::vector<const nlohmann::json*> pending{root};
    while(!pending.empty()) {
      const auto *value=pending.back();pending.pop_back();
      if(!JsonSeen.insert(value).second) continue;
      ++Extra.json_values;
      if(value->is_object()) {
        const auto &object=value->get_ref<const nlohmann::json::object_t&>();
        HeapObject(sizeof(object));Tree(object);
        for(const auto &entry:object) {Text(entry.first);pending.push_back(&entry.second);}
      } else if(value->is_array()) {
        const auto &array=value->get_ref<const nlohmann::json::array_t&>();
        HeapObject(sizeof(array));Vector(array);for(const auto &entry:array) pending.push_back(&entry);
      } else if(value->is_string()) {
        const auto &text=value->get_ref<const nlohmann::json::string_t&>();HeapObject(sizeof(text));Text(text);
      } else if(value->is_binary()) {
        const auto &binary=value->get_binary();HeapObject(sizeof(binary));
        Vector(static_cast<const nlohmann::json::binary_t::container_type&>(binary));
      }
    }
  }
  SourceInputStorageCensus Finish() {
    Extra.storage=Result;Extra.overflow=Extra.overflow||Result.overflow;
    if(Extra.RequestedHeapComponents()==std::numeric_limits<size_t>::max()) Extra.overflow=true;
    return Extra;
  }
};
}
// Caller keeps every supplied root and its containing storage live/quiescent.
// DOM nodes may also be supplied as roots in either order: count payload once.
// Root json cells are owned inline by the caller (or counted in parent backing),
// not an additional heap allocation. No lazy catalogs or retained strong refs.
inline SourceInputStorageCensus MeasureSourceInputStorage(
    const WorldGenSets *const *settings,size_t settings_count,
    const nlohmann::json *const *documents=nullptr,size_t document_count=0,
    const std::filesystem::path *const *paths=nullptr,size_t path_count=0)
{
  detail::SourceInputStorageInspector inspector;
  for(size_t i=0;i<settings_count;++i) inspector.Settings(settings[i]);
  for(size_t i=0;i<document_count;++i) inspector.Json(documents[i]);
  for(size_t i=0;i<path_count;++i) inspector.Path(paths[i]);
  return inspector.Finish();
}
} // namespace cutum
