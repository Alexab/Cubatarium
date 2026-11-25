#pragma once
#include "Test/OrdinaryNewObservation.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "WorldGen/Core/MergePublicationEnvelope.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <optional>

namespace merge_publication_fixture
{
using Json=nlohmann::json;
inline Json Textures(const cutum::UTextureBaseStorage &storage)
{
  Json out=Json::object();for(const auto &row:storage.GetBaseTextures()){
    const auto *pixels=row.second.GetPixelData();out[row.first]={{"name",row.second.GetName()},{"path",row.second.GetFileName()}};
    if(pixels)out[row.first]["pixels"]={{"w",pixels->Width},{"h",pixels->Height},{"rgba",pixels->Rgba}};
  }return out;
}
inline void Prior(cutum::UTextureBaseStorage &out)
{
  out.Register("pack/common","prior_common");out.Register("keep",std::string(4097,'k'));
  cutum::TexturePixelData p;p.Width=p.Height=1;p.Rgba={3,7,11,255};out.RegisterPixels("prior_pixels",std::move(p));
}
inline std::vector<cutum::ResourcePackManifest> Packs(const std::filesystem::path &root)
{
  std::vector<cutum::ResourcePackManifest> packs(3);
  for(int i=0;i<3;++i){auto &p=packs[i];p.Id=i==2?"absent":"pack";p.Root=root/std::to_string(i);p.EffectivePriority=i;
    if(i==2)continue;const auto dir=p.Root/"textures/blocks";std::filesystem::create_directories(dir);
    for(const auto *name:{"common.png","unique.png","ignored.PNG"}){std::ofstream out(dir/name);out<<"registration path marker";}
    std::filesystem::create_directories(dir/"directory.png");
  }return packs;
}
inline int Baseline(const char *path)
{
  using namespace cutum;
  const auto root=std::filesystem::path("build/desktop-msvc/s4-publication-fixtures");const auto packs=Packs(root);
  UBlockMergeRegistry merge;merge.Rebuild(packs,nullptr,16);std::vector<MergedCubeDesc> needed(1);needed[0].Stems.fill("missing_stem");
  Json report={{"schema","merge-publication-baseline.v1"},{"cases",Json::array()}};size_t reproduced=0;
  for(bool additive:{false,true}){
    std::optional<UTextureBaseStorage> out;out.emplace();Prior(*out);
    {observation::Scope scope;if(additive)merge.RegisterMissingTextureStems(*out,needed);else merge.PopulateTextureBaseStorage(*out);}
    const auto normal=observation::State;out.reset();if(observation::State.live || observation::State.blocks)return 1;
    for(size_t prefix=0;prefix<normal.attempts;++prefix){out.emplace();Prior(*out);const auto before=Textures(*out);bool refused=false;
      {observation::Scope scope(prefix);try{if(additive)merge.RegisterMissingTextureStems(*out,needed);else merge.PopulateTextureBaseStorage(*out);}catch(const std::bad_alloc &){refused=true;}}
      const bool changed=Textures(*out)!=before;out.reset();if(observation::State.live || observation::State.blocks)return 1;
      if(refused && changed){++reproduced;report["cases"].push_back({{"route",additive?"missing_stem_refresh":"full_replacement"},{"prefix",prefix},{"old_target_changed",true},{"normal_attempts",normal.attempts},{"normal_peak",normal.peak}});break;}
    }
  }
  report["status"]=reproduced==2?"REPRODUCED":"FAIL";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return reproduced==2?0:1;
}
inline cutum::TexturePublicationDomain TextureDomain(const std::vector<cutum::ResourcePackManifest> &packs,
    const std::shared_ptr<cutum::UPlaceholderTextureCache> &cache)
{
  cutum::TexturePublicationDomain d;d.packs=packs.size();d.placeholder=bool(cache);if(cache)d.cache=cutum::MeasurePlaceholderSourceInput(*cache);
  for(const auto &p:packs){d.id_max_bytes=std::max(d.id_max_bytes,p.Id.size());const auto dir=p.Root/"textures"/"blocks";
    d.max_path_units=std::max(d.max_path_units,dir.native().size());if(!std::filesystem::is_directory(dir))continue;
    for(const auto &e:std::filesystem::directory_iterator(dir)){d.max_path_units=std::max(d.max_path_units,e.path().native().size());if(e.path().extension()==".png")++d.png_rows;}
  }return d;
}
inline size_t TargetUpper(const cutum::UTextureBaseStorage &target)
{
  cutum::detail::CatalogStorageInspector inspector;target.InspectStorage(inspector);bool overflow=false;
  const auto result=cutum::detail::MergeCensusUpper(inspector.Result,overflow);return overflow?SIZE_MAX:result;
}
inline Json Definitions(const cutum::BlockDefinitionCatalog &catalog)
{
  Json result=Json::object();for(const auto &row:catalog.ById){const auto &d=row.second;result[d.Name]={{"id",row.first},{"definition_id",d.Id},{"display",d.DisplayName},{"hardness",d.Hardness},{"types",d.Types},{"dig",d.DigGroups}};}
  return result;
}
inline int Accepted(const char *path)
{
  using namespace cutum;namespace fs=std::filesystem;
  const auto root=fs::path("build/desktop-msvc/s4-publication-fixtures/accepted");auto packs=Packs(root/"textures");
  auto cache=std::make_shared<UPlaceholderTextureCache>(root/"cache",16,glm::vec3(0.2f),8);
  Json expected=Json::object();bool golden=true;
  for(int i=0;i<4;++i){const auto stem=cache->GetOrCreateStem("publication_"+std::to_string(i),0,16);
    std::ifstream in(root/"cache"/(stem+".rgba"),std::ios::binary);in.seekg(12);std::vector<uint8_t> pixels(1024);in.read(reinterpret_cast<char *>(pixels.data()),pixels.size());
    uint64_t hash=14695981039346656037ull;for(auto byte:pixels)hash=(hash^byte)*1099511628211ull;golden=golden && in.good() && hash==0xfbc66fccbb979734ull;
    expected[stem]={{"name",stem},{"path",""},{"pixels",{{"w",16},{"h",16},{"rgba",pixels}}}};
  }
  const auto dir=packs[1].Root/"textures"/"blocks";
  for(const auto *stem:{"common","unique","directory"}){const std::string key=std::string("pack/")+stem;expected[key]={{"name",key},{"path",(dir/(std::string(stem)+".png")).string()}};}
  auto merge=std::make_unique<UBlockMergeRegistry>();merge->Rebuild(packs,cache,16);
  std::vector<MergedCubeDesc> needed(1);needed[0].Stems.fill("missing_stem");
  Json report={{"schema","merge-publication-source.v1"},{"texture_cases",Json::array()},{"definition_cases",Json::array()}};size_t failures=golden?0:1,texturePrefixes=0,definitionPrefixes=0;
  for(bool additive:{false,true}){
    const auto domain=TextureDomain(packs,cache);const auto bound=BoundTexturePublication(domain);std::optional<UTextureBaseStorage> out;out.emplace();Prior(*out);
    const auto before=Textures(*out);auto wanted=additive?before:Json::object();for(auto it=expected.begin();it!=expected.end();++it)wanted[it.key()]=it.value();
    {observation::Scope scope;if(additive)merge->RegisterMissingTextureStems(*out,needed);else merge->PopulateTextureBaseStorage(*out);}
    const auto normal=observation::State;const bool ok=Textures(*out)==wanted && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper;
    out.reset();if(!ok || observation::State.live || observation::State.blocks)++failures;
    report["texture_cases"].push_back({{"route",additive?"missing_stem_refresh":"full_replacement"},{"pass",ok},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts},{"png_registration_rows",domain.png_rows}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){out.emplace();Prior(*out);bool refused=false;
      {observation::Scope scope(i);try{if(additive)merge->RegisterMissingTextureStems(*out,needed);else merge->PopulateTextureBaseStorage(*out);}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || Textures(*out)!=before || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      {observation::Scope scope;if(additive)merge->RegisterMissingTextureStems(*out,needed);else merge->PopulateTextureBaseStorage(*out);}
      if(Textures(*out)!=wanted)++failures;out.reset();if(observation::State.live || observation::State.blocks)++failures;++texturePrefixes;
    }
  }
  // The all-present route borrows all descriptor text and allocates nothing.
  UTextureBaseStorage have;Prior(have);std::vector<MergedCubeDesc> present(257);for(auto &d:present)d.Stems.fill("keep");
  const auto haveBefore=Textures(have);{observation::Scope scope(0);merge->RegisterMissingTextureStems(have,present);}
  const bool presentNoAlloc=observation::State.attempts==0 && Textures(have)==haveBefore;if(!presentNoAlloc)++failures;
  // Prepared commit must work with a persistent zero-allocation allowance.
  UTextureBaseStorage commit,prepared;Prior(commit);prepared.Register("pack/common","prepared_common");
  TexturePixelData p;p.Width=p.Height=1;p.Rgba={17,31,53,255};prepared.RegisterPixels("added",std::move(p));
  const auto *node=&prepared.GetBaseTextures().at("added");const auto *pixelData=node->GetPixelData()->Rgba.data();
  {observation::Scope scope(0);commit.MergePrepared(std::move(prepared));}
  bool noAllocCommit=observation::State.attempts==0 && &commit.GetBaseTextures().at("added")==node && node->GetPixelData()->Rgba.data()==pixelData &&
      commit.GetBaseTextures().at("pack/common").GetFileName()=="prepared_common" && commit.GetBaseTextures().count("keep")==1;
  UTextureBaseStorage swapped;swapped.Register("swapped","source");{observation::Scope scope(0);commit.Swap(swapped);commit.MergePrepared(std::move(commit));}
  noAllocCommit=noAllocCommit && !observation::State.attempts && commit.GetBaseTextures().count("swapped")==1;if(!noAllocCommit)++failures;
  // Prior and prepared targets coexist in one observation epoch.
  std::optional<UTextureBaseStorage> overlap;
  {observation::Scope scope;overlap.emplace();Prior(*overlap);}
  const auto entry=TargetUpper(*overlap);const auto entryObserved=observation::State.live;const auto bound=BoundTexturePublication(TextureDomain(packs,cache));
  observation::State.peak=observation::State.live;observation::State.active=true;merge->PopulateTextureBaseStorage(*overlap);observation::State.active=false;
  const auto peak=observation::State.peak;const bool combined=entryObserved<=entry && peak<=entry+bound.requested_heap_upper && Textures(*overlap)==expected;
  overlap.reset();if(!combined || observation::State.live || observation::State.blocks)++failures;
  report["texture_prior_current"]={{"pass",combined},{"entry_observed",entryObserved},{"entry_upper",entry},{"peak",peak},{"upper",entry+bound.requested_heap_upper}};
  // Definition construction publishes exactly once after all fallible copies.
  for(size_t rows:{size_t{0},size_t{17},size_t{65}}){
    auto inputs=Packs(root/("definitions_"+std::to_string(rows)));Json wanted=Json::object();
    const auto blockDir=inputs[0].Root/"blocks";fs::create_directories(blockDir);
    for(size_t i=0;i<rows;++i){const auto name="definition_"+std::to_string(i);const auto id=static_cast<BlockId>(kPackBlockIdMin+300+i);
      Json data={{"name",name},{"id",id},{"displayName",std::string(i==0?8193:129,'d')},{"hardness",float(i+1)},{"types",Json::array({"solid",std::string(33,'t')})},{"dig",{{"groups",{{std::string(37,'g'),int(i+2)}}}}}};
      data["textures"]=Json::array({"common","common","common","common","common","common"});
      {std::ofstream file(blockDir/(name+".json"));file<<data.dump();}
      wanted[name]={{"id",id},{"definition_id",id},{"display",data["displayName"]},{"hardness",float(i+1)},{"types",data["types"]},{"dig",data["dig"]["groups"]}};
    }
    UBlockMergeRegistry definitions;definitions.Rebuild(inputs,nullptr,16);const auto domain=InspectDefinitionPublication(definitions);const auto b=BoundDefinitionPublication(domain);
    const auto prepare=[](){auto out=std::make_unique<UBlockDefinitionStorage>();BlockDefinition old;old.Id=91;old.Name="prior_definition";old.DisplayName=std::string(8193,'o');out->ReplaceAll({{old.Id,old}},{{old.Name,old.Id}});return out;};
    auto out=prepare();auto prior=out->GetCatalogSnapshot();
    {observation::Scope scope;definitions.PopulateBlockDefinitionStorage(*out);}const auto normal=observation::State;
    const bool ok=Definitions(*out->GetCatalogSnapshot())==wanted && prior->ById.at(91).Name=="prior_definition" && b.supported && !b.overflow && normal.peak<=b.requested_heap_upper;
    out.reset();prior.reset();if(!ok || observation::State.live || observation::State.blocks)++failures;
    report["definition_cases"].push_back({{"rows",rows},{"pass",ok},{"peak",normal.peak},{"upper",b.requested_heap_upper},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){out=prepare();prior=out->GetCatalogSnapshot();bool refused=false;
      {observation::Scope scope(i);try{definitions.PopulateBlockDefinitionStorage(*out);}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || out->GetCatalogSnapshot()!=prior || observation::State.live || observation::State.blocks || observation::State.peak>b.requested_heap_upper)++failures;
      {observation::Scope scope;definitions.PopulateBlockDefinitionStorage(*out);}if(Definitions(*out->GetCatalogSnapshot())!=wanted || prior->ById.at(91).Name!="prior_definition")++failures;
      out.reset();prior.reset();if(observation::State.live || observation::State.blocks)++failures;++definitionPrefixes;
    }
    if(rows==65){
      {observation::Scope scope;out=prepare();}prior=out->GetCatalogSnapshot();const auto entryObserved=observation::State.live;
      size_t entryUpper=0;bool overflow=false;
      {detail::CatalogStorageInspector inspector;inspector.Blocks(prior);
        entryUpper=detail::MergeCensusUpper(inspector.Result,overflow)+sizeof(UBlockDefinitionStorage)+64;}
      observation::State.peak=observation::State.live;observation::State.active=true;definitions.PopulateBlockDefinitionStorage(*out);observation::State.active=false;
      const auto combinedPeak=observation::State.peak;
      const bool paired=!overflow && entryObserved<=entryUpper && combinedPeak<=entryUpper+b.requested_heap_upper &&
          prior->ById.at(91).DisplayName==std::string(8193,'o') && Definitions(*out->GetCatalogSnapshot())==wanted;
      out.reset();prior.reset();if(!paired || observation::State.live || observation::State.blocks)++failures;
      report["definition_prior_current"]={{"pass",paired},{"entry_observed",entryObserved},{"entry_upper",entryUpper},{"peak",combinedPeak},{"upper",entryUpper+b.requested_heap_upper}};
    }
  }
  // A real OS directory error preserves the target too;localized source bound.
  const auto errorRoot=root/"bad_directory";fs::create_directories(errorRoot/"textures");{std::ofstream file(errorRoot/"textures/blocks");file<<"regular file";}
  ResourcePackManifest bad;bad.Id="bad";bad.Root=errorRoot;UBlockMergeRegistry errorMerge;errorMerge.Rebuild({bad},nullptr,16);
  std::optional<std::filesystem::filesystem_error> firstError;UTextureBaseStorage errorTarget;Prior(errorTarget);
  try{errorMerge.PopulateTextureBaseStorage(errorTarget);}catch(const fs::filesystem_error &e){firstError=e;}
  bool fsPreserved=bool(firstError);size_t errorPeak=0,errorUpper=0;
  if(firstError){const auto error=BoundCatalogFilesystemErrorSource({64,firstError->code().message().size(),firstError->path1().native().size(),firstError->path2().native().size()});
    auto d=TextureDomain({bad},nullptr);d.extra_filesystem_diagnostic_upper=error.new_request_upper;errorUpper=BoundTexturePublication(d).requested_heap_upper;
    for(bool additive:{false,true}){const auto before=Textures(errorTarget);bool threw=false;
      {observation::Scope scope;try{if(additive)errorMerge.RegisterMissingTextureStems(errorTarget,needed);else errorMerge.PopulateTextureBaseStorage(errorTarget);}catch(const fs::filesystem_error &){threw=true;}}
      errorPeak=std::max(errorPeak,observation::State.peak);fsPreserved=fsPreserved && threw && Textures(errorTarget)==before && !observation::State.live && !observation::State.blocks && observation::State.peak<=errorUpper;
    }}
  if(!fsPreserved)++failures;report["filesystem_error"]={{"pass",fsPreserved},{"peak",errorPeak},{"upper",errorUpper}};
  std::optional<UTextureBaseStorage> escaped;escaped.emplace();{observation::Scope scope;merge->PopulateTextureBaseStorage(*escaped);}
  merge.reset();cache.reset();packs.clear();const bool owns=Textures(*escaped)==expected;escaped.reset();if(!owns || observation::State.live || observation::State.blocks)++failures;
  const auto hugeDef=BoundDefinitionPublication({SIZE_MAX,SIZE_MAX,true,false});TexturePublicationDomain huge;huge.png_rows=huge.max_path_units=huge.id_max_bytes=SIZE_MAX;
  const auto hugeTexture=BoundTexturePublication(huge);if(!hugeDef.overflow || !hugeTexture.overflow)++failures;
  bool negative=true;for(auto upper:{BoundDefinitionPublication({0,0,true,false}).requested_heap_upper,BoundTexturePublication({}).requested_heap_upper}){
    {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}negative=negative && observation::State.peak>upper && !observation::State.live;
  }if(!negative)++failures;
  report["pixel_golden"]=golden;report["all_present_zero_allocations"]=presentNoAlloc;report["prepared_commit_zero_allocations"]=noAllocCommit;report["texture_output_outlives_sources"]=owns;
  report["texture_refusal_and_retry_cases"]=texturePrefixes;report["definition_refusal_and_retry_cases"]=definitionPrefixes;report["overflow_rejected"]=hugeDef.overflow && hugeTexture.overflow;report["negative_controls"]=negative;
  report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Actual staged definition/texture publication,stable default MSVC filesystem profile plus real localized OS directory error. Prior/escaped targets separate;no concurrent texture RCU claim,OS/CRT/GPU/process cap or full C05 composition acceptance. PNG markers prove registration only;pixel oracle comes from real cached RGBA files/golden.";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace merge_publication_fixture
