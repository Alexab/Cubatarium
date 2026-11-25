#pragma once
#include "WorldGen/Core/ResourceBlockGrowthEnvelope.h"
static int RunResourceBlockGrowthFixture(const char *reportPath)
{
  Json report={{"schema","resource-block-growth-source.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  const Json input={{"name",std::string(129,'n')},{"displayName",std::string(257,'d')},{"id",kPackBlockIdMin+31},
    {"types",Json::array({std::string(257,'t'),"solid"})},{"dig",{{"groups",{{"pickaxe",4},{"shovel",3}}}}},
    {"textures",Json::array({std::string(5001,'a'),"b","c","d","e","f"})}};
  const auto make=[&](size_t n,size_t spare,bool buckets){std::vector<ResourcePackBlock> blocks;blocks.reserve(n+spare);
    for(size_t i=0;i<n;++i){auto p=ParseBlockFromJson(input,true);blocks.push_back(AdoptParsedResourceBlock(std::move(p)));if(buckets)blocks.back().Definition.DigGroups.reserve(257);}
    return blocks;};
  const auto verify=[&](const auto &blocks,size_t size){if(blocks.size()!=size)return false;for(const auto &b:blocks){if(b.Definition.Name!=std::string(129,'n') || b.Definition.DisplayName!=std::string(257,'d') || b.Definition.Id!=kPackBlockIdMin+31 || b.Definition.Types!=std::vector<std::string>{std::string(257,'t'),"solid"} || b.Definition.DigGroups.at("pickaxe")!=4 || b.Definition.DigGroups.at("shovel")!=3 || b.TextureStems[0]!=std::string(5001,'a') || b.TextureStems[5]!="f")return false;}return true;};
  const auto run=[&](const char *name,size_t n,size_t spare,bool buckets){
    auto blocks=make(n,spare,buckets);const auto payload=MeasureResourceBlockPayload(blocks);const auto bound=BoundResourceBlockAppend(blocks.size(),blocks.capacity(),payload);
    std::optional<ParsedBlockJson> parsed(ParseBlockFromJson(input,true));const auto oldCapacity=blocks.capacity();
    {observation::Scope scope;AppendResourcePackBlock(blocks,std::move(*parsed));}
    const auto normal=observation::State;const bool semantic=verify(blocks,n+1);{auto release=std::move(blocks);}const auto inputRetained=observation::State.live;parsed.reset();
    bool ok=semantic && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"size",n},{"capacity",oldCapacity},{"prior_payload_upper",payload.requested_heap_upper},{"new_source_upper",bound.requested_heap_upper},{"peak",normal.peak},{"attempts",normal.attempts},{"live_before_moved_input_destroy",inputRetained},{"final_live",observation::State.live}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){
      auto target=make(n,spare,buckets);std::optional<ParsedBlockJson> incoming(ParseBlockFromJson(input,true));const auto address=target.data();const auto capacity=target.capacity();
      const char *texture=n?target[0].TextureStems[0].data():nullptr;bool refused=false;
      {observation::Scope scope(i);try{AppendResourcePackBlock(target,std::move(*incoming));}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || target.data()!=address || target.capacity()!=capacity || !verify(target,n) || (n && target[0].TextureStems[0].data()!=texture))++failures;
      {auto release=std::move(target);}incoming.reset();if(observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      auto retry=make(n,spare,buckets);std::optional<ParsedBlockJson> retryInput(ParseBlockFromJson(input,true));{observation::Scope scope;AppendResourcePackBlock(retry,std::move(*retryInput));}
      if(!verify(retry,n+1))++failures;{auto release=std::move(retry);}retryInput.reset();if(observation::State.live || observation::State.blocks)++failures;++prefixes;
    }
  };
  run("empty",0,0,false);run("spare_no_array_growth",3,37,false);run("single_full",1,0,false);run("17_full_payload_copies",17,0,false);run("prior_spare_map_buckets",3,0,true);
  const auto overflow=BoundResourceBlockAppend(SIZE_MAX,SIZE_MAX,{SIZE_MAX,0,true,false});if(!overflow.overflow)++failures;
  const auto upper=BoundResourceBlockAppend(0,0,MeasureResourceBlockPayload({})).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["block_move_is_nothrow"]=std::is_nothrow_move_constructible_v<ResourcePackBlock>;
  report["persistent_prefix_and_retry_cases"]=prefixes;report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Additional ordinary-new append requests including existing-payload copies on vector growth;prior block array/payload and parsed input are separate live terms. Failed append preserves vector,may consume incoming parsed object. Not whole directory/merge/process bound.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
