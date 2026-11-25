#pragma once
#include "ResourcePacks/ResourcePack.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace cutum
{
// The legacy default-json object order is blocks,packs; each block is id,name
// and each pack is id,version. Pack order is the caller's original order.
// Borrowed rows remain stable for this call (registry callers hold read lock).
inline std::string BuildCatalogFingerprintPayload(const std::vector<ResourcePackManifest> &packs,
    const std::unordered_map<std::string,BlockId> &names)
{
  using Json=nlohmann::json;using Row=std::unordered_map<std::string,BlockId>::value_type;
  std::vector<const Row*> sorted;sorted.reserve(names.size());
  for(const auto &entry:names)sorted.push_back(&entry);
  std::sort(sorted.begin(),sorted.end(),[](const Row *a,const Row *b){return a->first<b->first;});
  std::string payload;
  nlohmann::detail::serializer<Json> serializer(nlohmann::detail::output_adapter<char>(payload),' ');
  // A scalar string owns at most one field backing. Its destructor does not
  // require the library's heap-allocated array/object retirement stack.
  Json text="";
  const auto string=[&](const std::string &value){text.get_ref<Json::string_t&>()=value;serializer.dump(text,false,false,0);};
  payload+="{\"blocks\":[";bool comma=false;
  for(const auto *entry:sorted){
    if(comma)payload+=',';comma=true;
    payload+="{\"id\":";serializer.dump(Json(entry->second),false,false,0);
    payload+=",\"name\":";string(entry->first);payload+='}';
  }
  payload+="],\"packs\":[";comma=false;
  for(const auto &pack:packs){
    if(comma)payload+=',';comma=true;
    payload+="{\"id\":";string(pack.Id);
    payload+=",\"version\":";serializer.dump(Json(pack.Version),false,false,0);payload+='}';
  }
  payload+="]}";return payload;
}
} // namespace cutum
