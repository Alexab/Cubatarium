#pragma once
#include "WorldGen/Core/WorldGenSets.h"
#include <algorithm>
#include <limits>

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
} // namespace cutum
