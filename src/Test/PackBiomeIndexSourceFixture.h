#pragma once
#include "WorldGen/Core/PackBiomeIndexSourceEnvelope.h"
static int RunPackBiomeIndexSourceFixture(const char *reportPath)
{
  Json report={{"schema","pack-biome-index-source.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  const auto active=UWorldGenPack::GetSnapshot();
  const std::vector<std::string> builtin={"plains","forest","desert","hills","tundra","savanna","foothills","scrubland","cold_steppe","ocean","beach","mountains","snowy_slopes","jagged_peaks","lush_caves","dripstone_caves","deep_dark"};
  size_t builtinBytes=0;for(const auto &k:builtin)builtinBytes+=k.size();if(builtin.size()!=17 || builtinBytes>=512)++failures;
  const auto verify=[&](const WorldGenPack &pack,const WorldGenPackBiomeIndex &index){
    auto keys=builtin;for(const auto &entry:pack.Biomes)keys.push_back(entry.first);std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
    if(!index.Registry || index.Registry->Size()!=keys.size() || index.Registry->Find("absent_key").IsValid() || index.Registry->Key(BiomeHandle{}))return false;
    std::vector<BiomeHandle> surface,cave;
    for(size_t i=0;i<keys.size();++i){const BiomeHandle h{static_cast<uint32_t>(i)};const auto *key=index.Registry->Key(h);
      if(!key || *key!=keys[i] || index.Registry->Find(keys[i])!=h)return false;
      if(std::find(builtin.begin(),builtin.end(),keys[i])!=builtin.end())continue;
      const auto found=pack.Biomes.find(keys[i]);if(found==pack.Biomes.end() || !found->second.Climate.Enabled)continue;
      (found->second.Domain==BiomeDomain::Surface?surface:cave).push_back(h);
    }
    return surface==index.Surface && cave==index.Cave;
  };
  const auto run=[&](const char *name,WorldGenPack &pack){
    size_t text=0;for(const auto &entry:pack.Biomes)text+=entry.first.size();const auto bound=BoundPackBiomeIndexSource(pack.Biomes.size(),text);
    const auto oldRegistry=pack.BiomeRegistry;const auto oldSurface=pack.SurfaceClimateBiomes,oldCave=pack.CaveClimateBiomes;
    const auto unchanged=[&]{return pack.BiomeRegistry==oldRegistry && pack.SurfaceClimateBiomes==oldSurface && pack.CaveClimateBiomes==oldCave && UWorldGenPack::GetSnapshot()==active;};
    std::optional<WorldGenPackBiomeIndex> result;
    {observation::Scope scope;result.emplace(CompileWorldGenPackBiomeIndex(pack));}
    const auto normal=observation::State;const bool semantic=verify(pack,*result);result.reset();
    const bool ok=semantic && unchanged() && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"keys",pack.Biomes.size()},{"text",text},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){
      bool refused=false;{observation::Scope scope(i);try{result.emplace(CompileWorldGenPackBiomeIndex(pack));}catch(const std::bad_alloc &){refused=true;}}
      result.reset();if(!refused || !unchanged() || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      {observation::Scope scope;result.emplace(CompileWorldGenPackBiomeIndex(pack));}if(!verify(pack,*result))++failures;result.reset();
      if(!unchanged() || observation::State.live || observation::State.blocks)++failures;++prefixes;
    }
  };
  WorldGenPack empty;run("builtin_only",empty);
  WorldGenPack duplicates;for(const auto &k:builtin){BiomePackDefinition d;d.Climate.Enabled=true;duplicates.Biomes.emplace(k,std::move(d));}run("builtin_duplicates",duplicates);
  WorldGenPack mixed;for(int i=0;i<3;++i){BiomePackDefinition d;d.Climate.Enabled=i!=2;d.Domain=i?BiomeDomain::Cave:BiomeDomain::Surface;mixed.Biomes.emplace("custom_"+std::to_string(i),std::move(d));}
  mixed.BiomeRegistry=std::make_shared<const UBiomeDefinitionRegistry>(std::vector<std::string>{"old_key"});mixed.SurfaceClimateBiomes={{7}};mixed.CaveClimateBiomes={{8}};run("mixed_and_prior_outputs",mixed);
  WorldGenPack wide;for(int i=0;i<257;++i){BiomePackDefinition d;d.Climate.Enabled=true;d.Domain=i%2?BiomeDomain::Surface:BiomeDomain::Cave;wide.Biomes.emplace("custom_"+std::to_string(i),std::move(d));}run("257_keys_rehash_both_domains",wide);
  WorldGenPack longKeys;BiomePackDefinition d;d.Climate.Enabled=true;longKeys.Biomes.emplace(std::string(8193,'l'),std::move(d));run("long_key_direct_api",longKeys);
  // Returned index owns key storage after its borrowed candidate disappears.
  std::optional<WorldGenPackBiomeIndex> escaped;
  {WorldGenPack input;input.Biomes.emplace("escaped_custom",BiomePackDefinition{});{observation::Scope scope;escaped.emplace(CompileWorldGenPackBiomeIndex(input));}}
  const bool owned=escaped->Registry->Find("escaped_custom").IsValid();escaped.reset();if(!owned || observation::State.live || observation::State.blocks)++failures;
  const auto max=static_cast<size_t>(std::numeric_limits<uint32_t>::max());
  const bool handles=BiomeHandleCountFits(0) && BiomeHandleCountFits(max) && !BiomeHandleCountFits(max+size_t{1});if(!handles)++failures;
  const auto overflow=BoundPackBiomeIndexSource(SIZE_MAX,SIZE_MAX);if(!overflow.overflow)++failures;
  const auto upper=BoundPackBiomeIndexSource(0,0).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["returned_index_owns_keys"]=owned;report["handle_count_boundaries"]=handles;report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;report["persistent_prefix_and_retry_cases"]=prefixes;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Standalone finalization result,new registry/control and key/map/climate-vector backing;borrowed Biomes/prior outputs and escaped versions separate. No real huge allocation for integer boundary. Not whole directory/process memory cap.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
