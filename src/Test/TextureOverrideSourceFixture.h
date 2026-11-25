#pragma once
#include "WorldGen/Core/TextureOverrideSourceEnvelope.h"
#include <sstream>
static int RunTextureOverrideSourceFixture(const char *reportPath)
{
  Json report={{"schema","texture-override-source.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  const auto same=[](const TextureOverrideMap &a,const TextureOverrideMap &b){if(a.size()!=b.size())return false;for(const auto &[name,rows]:a){const auto it=b.find(name);if(it==b.end() || rows.size()!=it->second.size())return false;for(size_t i=0;i<rows.size();++i)if(rows[i].Faces!=it->second[i].Faces || rows[i].Stem!=it->second[i].Stem)return false;}return true;};
  const auto run=[&](const char *name,auto prepare,auto produce,const TextureOverrideMap &expected,CatalogCandidateEnvelope bound){
    auto input=prepare();std::optional<TextureOverrideMap> result;
    {observation::Scope scope;result.emplace(produce(input));}const auto normal=observation::State;
    const bool semantic=same(*result,expected);result.reset();input.reset();
    bool ok=semantic && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){input=prepare();bool threw=false;
      {observation::Scope scope(i);try{result.emplace(produce(input));}catch(const std::bad_alloc &){threw=true;}}
      const auto counts=observation::State;result.reset();input.reset();if(!counts.refusals || observation::State.live || observation::State.blocks || counts.peak>bound.requested_heap_upper)++failures;
      input=prepare();{observation::Scope scope;result.emplace(produce(input));}if(!same(*result,expected))++failures;result.reset();input.reset();if(observation::State.live || observation::State.blocks)++failures;++prefixes;(void)threw;
    }
  };
  const auto jsonCase=[&](const char *name,const Json &input,const TextureOverrideMap &expected){run(name,[&]{return std::make_unique<int>(0);},[&](const auto &){return CompileTextureOverridesJson(input);},expected,BoundTextureOverrideJsonSource(MeasureCatalogCandidateInput(input)));};
  Json input={{"plant",Json::array({{{"stem","side_tex"},{"faces",Json::array({"front","east","north","west","top","bottom","sides","unknown",42})}},{{"stem","default_tex"}},{{"faces",Json::array({"top"})}}})},
    {"wrapper",{{"overrides",Json::array({{{"stem",std::string(5001,'s')},{"faces",Json::array({"side","side"})}}})}}},{"single",{{"stem","one"},{"faces",Json::array({"unknown"})}}},{"ignored",42}};
  TextureOverrideMap expected;expected["plant"]={{{0,1,2,3,4,5,0,1,2,3},"side_tex"},{{0,1,2,3,4,5},"default_tex"}};
  expected["wrapper"]={{{0,1,2,3,0,1,2,3},std::string(5001,'s')}};expected["single"]={{{0,1,2,3,4,5},"one"}};
  jsonCase("json_forms_aliases_defaults",input,expected);
  jsonCase("json_nonobject",Json::array(),{});jsonCase("json_empty",Json::object(),{});
  Json wide=Json::object();TextureOverrideMap wideExpected;
  for(int i=0;i<129;++i){const auto name="block"+std::to_string(i);wide[name]={{"stem",std::string(129,'t')},{"faces",Json::array({"side"})}};wideExpected[name]={{{0,1,2,3},std::string(129,'t')}};}
  jsonCase("json129_map_rehash",wide,wideExpected);
  const auto yamlCase=[&](const char *name,const std::string &source,const TextureOverrideMap &expected){run(name,[&]{return std::make_unique<std::istringstream>(source);},[](const auto &stream){return ParseTextureOverridesYaml(*stream);},expected,BoundTextureOverrideYamlSource(MeasureTextureOverrideYamlInput(source)));};
  const std::string yaml=" # comment\nplant:\n faces: [front, east, sides]\n stem: first\n - next\n stem: second\n\nignored text\nother:\n faces: [unknown]\n stem: "+std::string(5001,'y')+"\n";
  TextureOverrideMap yamlExpected;yamlExpected["plant"]={{{0,1,0,1,2,3},"first"},{{0,1,2,3,4,5},"second"}};yamlExpected["other"]={{{0,1,2,3,4,5},std::string(5001,'y')}};
  yamlCase("yaml_subset_aliases_defaults",yaml,yamlExpected);yamlCase("yaml_empty","",{});
  std::string wideYaml;TextureOverrideMap wideYamlExpected;
  for(int i=0;i<129;++i){const auto name="yaml"+std::to_string(i);wideYaml+=name+":\n stem: "+std::string(129,'z')+"\n";wideYamlExpected[name]={{{0,1,2,3,4,5},std::string(129,'z')}};}
  yamlCase("yaml129_map_rehash",wideYaml,wideYamlExpected);
  const std::string repeated="same:\n stem: old\nsame:\n faces: [top]\n stem: latest\n";TextureOverrideMap repeatedExpected;repeatedExpected["same"]={{{4},"latest"}};yamlCase("yaml_repeated_block",repeated,repeatedExpected);
  const auto parent=std::filesystem::temp_directory_path()/(L"cuba_overrides_"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(parent);
  {std::ofstream out(parent/"texture_overrides.yaml");out<<yaml;}
  {std::ofstream out(parent/"texture_overrides.json",std::ios::binary);out<<"\xEF\xBB\xBF"<<input.dump();}
  const bool preference=same(LoadTextureOverrides(parent),expected);if(!preference)++failures;
  const auto before=GetCatalogJsonSourceSnapshot();
  std::optional<TextureOverrideMap> loaded;{observation::Scope scope;loaded.emplace(LoadTextureOverrides(parent));}const auto fileCounts=observation::State;
  if(!same(*loaded,expected))++failures;loaded.reset();if(observation::State.live || observation::State.blocks)++failures;
  size_t filePrefixes=0;
  for(size_t i=0;i<fileCounts.attempts;++i){
    {observation::Scope scope(i);try{loaded.emplace(LoadTextureOverrides(parent));}catch(const std::bad_alloc &){}}
    const bool refused=observation::State.refusals>0;loaded.reset();if(!refused || observation::State.live || observation::State.blocks)++failures;
    loaded.emplace(LoadTextureOverrides(parent));if(!same(*loaded,expected))++failures;loaded.reset();++filePrefixes;
  }
  const auto after=GetCatalogJsonSourceSnapshot();const bool kind=after.attempts[static_cast<size_t>(CatalogJsonKind::Overrides)]>before.attempts[static_cast<size_t>(CatalogJsonKind::Overrides)];if(!kind)++failures;
  const bool fileClosed=CanOpenCatalogFileExclusively(parent/"texture_overrides.json");if(!fileClosed)++failures;
  {std::ofstream out(parent/"texture_overrides.json");out<<"{\"broken\":[{\"stem\":\"partial\"}";}
  const bool malformed=LoadTextureOverrides(parent).empty();if(!malformed)++failures;
  {std::ofstream out(parent/"texture_overrides.json");out<<R"({"same":{"stem":"old"},"same":{"stem":"latest","faces":["top"]}})";}
  TextureOverrideMap lastExpected;lastExpected["same"]={{{4},"latest"}};const bool duplicate=same(LoadTextureOverrides(parent),lastExpected);if(!duplicate)++failures;
  std::filesystem::remove(parent/"texture_overrides.json");if(!same(LoadTextureOverrides(parent),yamlExpected))++failures;
  {observation::Scope scope;loaded.emplace(LoadTextureOverrides(parent));}const auto yamlFileCounts=observation::State;
  if(!same(*loaded,yamlExpected))++failures;loaded.reset();if(observation::State.live || observation::State.blocks)++failures;
  size_t yamlFilePrefixes=0;
  for(size_t i=0;i<yamlFileCounts.attempts;++i){
    {observation::Scope scope(i);try{loaded.emplace(LoadTextureOverrides(parent));}catch(const std::bad_alloc &){}}
    const bool refused=observation::State.refusals>0;loaded.reset();if(!refused || observation::State.live || observation::State.blocks)++failures;
    loaded.emplace(LoadTextureOverrides(parent));if(!same(*loaded,yamlExpected))++failures;loaded.reset();++yamlFilePrefixes;
  }
  if(!CanOpenCatalogFileExclusively(parent/"texture_overrides.yaml"))++failures;
  const auto resolved=std::filesystem::weakly_canonical(parent),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;std::filesystem::remove_all(resolved);
  const auto overflow=BoundTextureOverrideConversion(SIZE_MAX,SIZE_MAX);if(!overflow.overflow)++failures;
  const auto upper=BoundTextureOverrideJsonSource(MeasureCatalogCandidateInput(input)).requested_heap_upper;{observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["persistent_prefix_and_retry_cases"]=prefixes;report["file_prefix_and_retry_cases"]=filePrefixes;report["yaml_file_prefix_and_retry_cases"]=yamlFilePrefixes;
  report["malformed_json_no_yaml_fallback"]=malformed;report["duplicate_keys_last_wins"]=duplicate;report["process_counters_inline_bytes"]=sizeof(CatalogJsonSourceSnapshot);
  report["json_precedes_yaml"]=preference;report["override_kind_recorded"]=kind;report["file_closed"]=fileClosed;report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Shared JSON conversion and existing YAML subset source requests;DOM/istream/ifstream/path/parser/prior outputs separate. Real file loader includes managed parse/close/refusal cleanup. YAML may return partial result when stream sets badbit,matching prior policy;retry exact input required.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
