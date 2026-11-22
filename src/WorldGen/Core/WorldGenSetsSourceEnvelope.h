#pragma once
#include "WorldGen/Core/WorldGenSets.h"
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

namespace cutum
{
struct ResolvedFeaturesSourceEnvelope
{
  size_t requested_heap_upper{},selected_rules{},selected_cave_rules{};
  bool overflow{},supported{};
};
// Parameterized construction bound for ResolveObjectFeatures only. Input
// WorldGenSets/global/previous publications remain separate owners. Current
// MSVC Release requested ordinary-new bytes, including STL alignment slack;
// not CRT headers, stacks, parser, diagnostic preparation, shared control/RSS.
inline ResolvedFeaturesSourceEnvelope BoundResolvedFeaturesSource(
    const WorldGenSets &sets,const ObjectFeatureConfig &global,int profile=2)
{
  ResolvedFeaturesSourceEnvelope result;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  result.supported=true;
#endif
  const auto add=[&](size_t n) {
    if(n>std::numeric_limits<size_t>::max()-result.requested_heap_upper) {
      result.overflow=true;result.requested_heap_upper=std::numeric_limits<size_t>::max();
    } else result.requested_heap_upper+=n;
  };
  const auto term=[&](size_t n,size_t width) {
    if(width && n>std::numeric_limits<size_t>::max()/width) {
      result.overflow=true;result.requested_heap_upper=std::numeric_limits<size_t>::max();
    } else add(n*width);
  };
  const auto vector=[&](size_t n,size_t width) {term(n,width);if(n) add(39);};
  const auto text=[&](const std::string &s) {
    // Copy capacity rounding/alignment and legacy string replacement old+new
    // growth (<=1.5 old capacity or rounded new length). Counts SSO too.
    term(s.size(),3);add(192);
  };
  const auto profileText=[&](const auto &r) {
    text(r.Tree.TrunkBlock);text(r.Tree.LeavesBlock);
    text(r.Shrub.StemBlock);text(r.Shrub.LeavesBlock);text(r.Shrub.AccentBlock);
  };
  const auto fields=[&](const auto &r) {text(r.ObjectName);text(r.Scatter.BlockName);profileText(r);};
  const auto legacy=[&](const auto &r,const auto &fallback) {
    if(sets.SchemaVersion!=1 || profile<2) return;
    const auto authored=std::find_if(fallback.begin(),fallback.end(),[&](const auto &a) {
      if(r.Mode==ObjectFeatureRuleMode::ScatterBlocks || a.Mode==ObjectFeatureRuleMode::ScatterBlocks)
        return r.Mode==ObjectFeatureRuleMode::ScatterBlocks && a.Mode==ObjectFeatureRuleMode::ScatterBlocks && r.Scatter.BlockName==a.Scatter.BlockName;
      return r.ObjectName==a.ObjectName;
    });
    if(authored!=fallback.end() && (r.Mode==ObjectFeatureRuleMode::Object || r.Mode==authored->Mode)) profileText(*authored);
  };
  const auto pool=[&](const auto &sections,const auto &fallback) {
    size_t count=0;
    if(sections.empty()) {
      count=fallback.size();
      for(const auto &r:fallback) {
        fields(r);vector(r.Biomes.size(),sizeof(BiomeId));vector(r.BiomeKeys.size(),sizeof(BiomeKey));
        vector(r.SubBiomes.size(),sizeof(SubBiomeId));for(const auto &key:r.BiomeKeys) text(key);
        legacy(r,fallback);
      }
    } else {
      for(const auto &section:sections) for(const auto &r:section.second.Entries) {
        if(r.Mode==ObjectFeatureRuleMode::ScatterBlocks?r.Scatter.BlockName.empty():r.ObjectName.empty()) continue;
        if(count==std::numeric_limits<size_t>::max()) result.overflow=true;else ++count;
        fields(r);vector(r.Biomes.size(),sizeof(BiomeId));vector(r.Biomes.size(),sizeof(BiomeKey));
        vector(r.SubBiomes.size(),sizeof(SubBiomeId));for(const auto &key:r.Biomes) text(key);
        legacy(r,fallback);
      }
    }
    vector(count,sizeof(ObjectFeatureRule));
    if(count>std::numeric_limits<size_t>::max()-result.selected_rules) result.overflow=true;
    else result.selected_rules+=count;
  };
  pool(sets.VegetationSections,global.Vegetation);pool(sets.GroundCoverSections,global.GroundCover);
  pool(sets.DecorationSections,global.Decoration);pool(sets.StructureSections,global.Structures);
  result.selected_cave_rules=global.CaveFeatures.size();vector(global.CaveFeatures.size(),sizeof(CaveFeatureRule));
  for(const auto &r:global.CaveFeatures) {text(r.BiomeKeyName);text(r.BlockName);}
  return result;
}

struct WorldGenSetsConstructionEnvelope
{
  size_t requested_heap_upper{},input_values{},input_text_bytes{},entries{};
  bool overflow{},supported{};
};
namespace detail
{
struct SettingsConstructionBudget
{
  WorldGenSetsConstructionEnvelope Result;
  SettingsConstructionBudget() {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    Result.supported=true;
#endif
  }
  void Add(size_t &out,size_t n) {
    if(n>std::numeric_limits<size_t>::max()-out) {Result.overflow=true;out=std::numeric_limits<size_t>::max();}
    else out+=n;
  }
  void Bytes(size_t n) {Add(Result.requested_heap_upper,n);}
  void Term(size_t n,size_t width) {
    if(width && n>std::numeric_limits<size_t>::max()/width) {Result.overflow=true;Result.requested_heap_upper=std::numeric_limits<size_t>::max();}
    else Bytes(n*width);
  }
  void Text(const std::string &s) {Term(s.size(),8);Bytes(128);}
  void VectorGrowth(size_t n,size_t width) {Term(n,width);Term(n,39);}
  void MapStorage(size_t nodes) {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    using Sections=decltype(WorldGenSets::VegetationSections);
    using Terrain=decltype(WorldGenSets::Terrain);
    using Ores=decltype(WorldGenSets::Ores);
    constexpr size_t node=std::max({sizeof(std::_List_node<Sections::value_type,void*>),
      sizeof(std::_List_node<Terrain::value_type,void*>),sizeof(std::_List_node<Ores::value_type,void*>)});
    // Six fresh maps, each sentinel+8 buckets (2 unchecked iterators/bucket).
    // Reset's temporary and result are conservatively allowed simultaneously.
    Bytes(12*(node+8*2*sizeof(void*)+39));
    Term(nodes,node);
    // Current MSVC rehash: <512 buckets can grow8x, afterwards powers of2.
    // Simultaneous old+new <16 buckets per admitted node; alignment separately.
    Term(nodes,16*2*sizeof(void*)+2*39);
#endif
  }
};
}

// Covers application DOM->sets construction/partial exceptions, not allocation
// of the supplied DOM, prior output/error capacity, CRT/exception runtime or
// native stack. Shape traversal is iterative; its pointer-vector growth is
// included as a separate phase, never summed as a simultaneous parser peak.
inline WorldGenSetsConstructionEnvelope BoundWorldGenSetsParseSource(const nlohmann::json &root)
{
  detail::SettingsConstructionBudget budget;
  size_t texts=0;
  std::vector<const nlohmann::json*> pending{&root};
  while(!pending.empty()) {
    const auto *value=pending.back();pending.pop_back();
    budget.Add(budget.Result.input_values,1);
    if(value->is_string()) {budget.Add(texts,1);budget.Add(budget.Result.input_text_bytes,value->get_ref<const std::string&>().size());}
    if(value->is_object() || value->is_array()) for(auto it=value->begin();it!=value->end();++it) {
      if(value->is_object()) {budget.Add(texts,1);budget.Add(budget.Result.input_text_bytes,it.key().size());}
      pending.push_back(&*it);
    }
  }
  const size_t values=budget.Result.input_values;
  budget.MapStorage(values);
  // All entry/vector admissions <= DOM values.4x live old+new vector growth,
  // including the temporary entry's two string lists and inspector pointers.
  budget.VectorGrowth(values,4*(sizeof(WorldGenObjectEntry)+2*sizeof(std::string)+sizeof(void*)));
  budget.Term(budget.Result.input_text_bytes,8);budget.Term(texts,128);
  // Current json::value/type diagnostics and fixed built-in mode-registry cold
  // initialization. Registry's persistent roots remain C06/E12, not outputs.
  budget.Bytes(16384);
  return budget.Result;
}

// BuildDefaultWorldGenSets reads one immutable global source. Its source
// payload/read handle is retained separately; returned section growth and
// FromFeatureRule's text/list copies are bounded here (no diagnostic prepare).
inline WorldGenSetsConstructionEnvelope BoundDefaultWorldGenSetsSource(const ObjectFeatureConfig &source)
{
  detail::SettingsConstructionBudget budget;budget.MapStorage(4);
  const auto pool=[&](const auto &rules) {
    budget.Add(budget.Result.entries,rules.size());
    budget.VectorGrowth(rules.size(),4*sizeof(WorldGenObjectEntry));
    budget.Text(std::string("default"));
    for(const auto &r:rules) {
      budget.Text(r.ObjectName);budget.Text(r.Scatter.BlockName);budget.Text(r.Tree.TrunkBlock);budget.Text(r.Tree.LeavesBlock);
      budget.Text(r.Shrub.StemBlock);budget.Text(r.Shrub.LeavesBlock);budget.Text(r.Shrub.AccentBlock);
      const size_t biomes=r.BiomeKeys.empty()?r.Biomes.size():r.BiomeKeys.size();
      budget.VectorGrowth(biomes,4*sizeof(std::string));budget.VectorGrowth(r.SubBiomes.size(),4*sizeof(std::string));
      if(r.BiomeKeys.empty()) budget.Term(biomes,8*64+128); // all built-in enum spellings <64 chars
      else for(const auto &key:r.BiomeKeys) budget.Text(key);
      budget.Term(r.SubBiomes.size(),8*64+128);
    }
  };
  pool(source.Vegetation);pool(source.GroundCover);pool(source.Decoration);pool(source.Structures);
  return budget.Result;
}

// In-place legacy upgrade can reuse arbitrary caller capacities. Include their
// possible1.5x assignment growth, not merely current string lengths. Existing
// sets/global ownership is separate; no parser, serialization or RSS claim.
inline WorldGenSetsConstructionEnvelope BoundWorldGenSetsUpgradeSource(
    const WorldGenSets &sets,const ObjectFeatureConfig &source,int profile)
{
  detail::SettingsConstructionBudget budget;
  if(profile<2 || sets.SchemaVersion!=1) return budget.Result;
  const auto replace=[&](const std::string &old,const std::string &next) {
    budget.Term(old.capacity(),3);budget.Term(next.size(),3);budget.Bytes(192);
  };
  const auto pool=[&](const auto &sections,const auto &rules) {
    for(const auto &section:sections) for(const auto &entry:section.second.Entries) {
      const auto authored=std::find_if(rules.begin(),rules.end(),[&](const auto &r){return r.ObjectName==entry.ObjectName;});
      if(authored==rules.end() || (entry.Mode!=ObjectFeatureRuleMode::Object && entry.Mode!=authored->Mode)) continue;
      budget.Add(budget.Result.entries,1);
      replace(entry.Tree.TrunkBlock,authored->Tree.TrunkBlock);replace(entry.Tree.LeavesBlock,authored->Tree.LeavesBlock);
      replace(entry.Shrub.StemBlock,authored->Shrub.StemBlock);replace(entry.Shrub.LeavesBlock,authored->Shrub.LeavesBlock);
      replace(entry.Shrub.AccentBlock,authored->Shrub.AccentBlock);
    }
  };
  pool(sets.VegetationSections,source.Vegetation);pool(sets.GroundCoverSections,source.GroundCover);
  pool(sets.DecorationSections,source.Decoration);pool(sets.StructureSections,source.Structures);
  return budget.Result;
}
} // namespace cutum
