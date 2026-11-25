#pragma once
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include <optional>

static int RunCatalogCandidateSourceFixture(const char *reportPath)
{
  Json report={{"schema","catalog-candidate-source-requests.v1"},{"status","PASS"},{"cases",Json::array()}};
  size_t failures=0,persistent=0;
  (void)UObjectFeatureTypeRegistry::Find("object");
  const auto activeFeatures=UObjectFeatureConfigStorage::GetSnapshot();const auto activeRefs=UWorldGenRefs::GetSnapshot();
  const auto run=[&](const std::string &name,CatalogCandidateKind kind,const Json &input,size_t pathUnits,
                     auto produce,auto oracle,int expectedException=0,bool failureMatrix=true) {
    const auto domain=MeasureCatalogCandidateInput(input);const auto bound=BoundCatalogCandidateSource(kind,domain,pathUnits);
    using T=decltype(produce());std::optional<T> candidate;int exception=0;
    {observation::Scope scope;try{candidate.emplace(produce());}catch(const Json::exception &e){exception=e.id;}}
    auto counts=observation::State;
    const bool value=exception==expectedException && (expectedException!=0 || (candidate && oracle(*candidate)));
    candidate.reset();counts.live=observation::State.live;counts.blocks=observation::State.blocks;
    bool ok=value && bound.supported && !bound.overflow && counts.live==0 && counts.blocks==0 && counts.peak<=bound.requested_heap_upper;
    if(!ok) ++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"values",domain.values},{"text_bytes",domain.text_bytes},
      {"path_units",pathUnits},{"source_upper",bound.requested_heap_upper},{"shared_blocks_included",bound.shared_blocks_bytes},
      {"peak",counts.peak},{"attempts",counts.attempts},{"exception_id",exception},{"final_live",counts.live}});
    if(ok && !expectedException && failureMatrix) for(size_t i=0;i<counts.attempts;++i) {
      bool refused=false;{observation::Scope scope(i);try{candidate.emplace(produce());}catch(const std::bad_alloc &){refused=true;}}
      candidate.reset();
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper) ++failures;
      {observation::Scope scope;candidate.emplace(produce());}
      if(!oracle(*candidate)) ++failures;candidate.reset();
      if(observation::State.live || observation::State.blocks) ++failures;
      if(UObjectFeatureConfigStorage::GetSnapshot()!=activeFeatures || UWorldGenRefs::GetSnapshot()!=activeRefs) ++failures;
      ++persistent;
    }
  };
  // Explicit semantic fixtures:all pools,authored key retention,normalization,
  // numeric diagnostics,ignored rows and deep/long/wide source domains.
  ObjectFeatureRule rule;rule.ObjectName=std::string(129,'o');rule.BiomeKeys={"plains",std::string(257,'b')};
  rule.Biomes={BiomeId::Plains};rule.SubBiomes={SubBiomeId::Woodland,SubBiomeId::Default};
  rule.Spacing=13;rule.Weight=1;rule.ChancePerColumn=17;rule.SeedOffset=19;rule.PlacementYOffset=-2;
  const Json row={{"object",rule.ObjectName},{"biomes",Json::array({"plains",std::string(257,'b'),42})},
    {"sub_biomes",Json::array({"woodland","unknown",false})},{"spacing",13},{"weight",-3},
    {"chance_per_column",17},{"seed_offset",19},{"placement_y_offset",-2}};
  Json features;for(const char *pool:{"vegetation","ground_cover","decoration","structures"}) features[pool]=Json::array({row,Json::object()});
  features["cave_features"]=Json::array({{{"biome","lush_caves"},{"kind","moss_patch"},{"chance",0},{"radius",99},{"max_height",99}}});
  features["structure_placement"]={{"cell_size",1},{"chance_per_cell",0},{"min_spacing",2}};
  const auto featureOracle=[&](const auto &value) {
    if(!value || value->structureCellSize!=16 || value->structureChancePerCell!=1 || value->structureMinSpacing!=32) return false;
    for(const auto *pool:{&value->Vegetation,&value->GroundCover,&value->Decoration,&value->Structures})
      if(pool->size()!=1 || !SameRule(pool->front(),rule)) return false;
    const auto diag=value->Diagnostics.Snapshot();
    return diag && diag->RuleCount()==1 && value->CaveFeatures.size()==1 && value->CaveFeatures[0].BlockName=="moss_block" &&
      value->CaveFeatures[0].Chance==1 && value->CaveFeatures[0].Radius==4 && value->CaveFeatures[0].MaxHeight==6;
  };
  run("features_all_pools",CatalogCandidateKind::Features,features,0,[&]{return CompileObjectFeatureCatalog(features);},featureOracle);
  {
    auto wide=features;wide["decoration"]=Json::array();for(int i=0;i<129;++i) wide["decoration"].push_back(row);
    run("features129_diagnostics_and_growth",CatalogCandidateKind::Features,wide,0,[&]{return CompileObjectFeatureCatalog(wide);},[&](const auto &v){
      if(!v || v->Decoration.size()!=129 || !std::all_of(v->Decoration.begin(),v->Decoration.end(),[&](const auto &r){return SameRule(r,rule);})) return false;
      const auto diag=v->Diagnostics.Snapshot();return diag && diag->RuleCount()==129;
    });
  }
  for(const auto &input:std::vector<Json>{Json{},Json::array(),Json(42)})
    run("features_nonobject",CatalogCandidateKind::Features,input,0,[&]{return CompileObjectFeatureCatalog(input);},[](const auto &v){return !v;});
  {Json empty=Json::object();run("features_empty",CatalogCandidateKind::Features,empty,0,[&]{return CompileObjectFeatureCatalog(empty);},[](const auto &v){return v && v->Decoration.empty() && v->Diagnostics.Snapshot();});}
  {auto bad=features;bad["vegetation"][0]["object"]=42;run("features_wrong_name",CatalogCandidateKind::Features,bad,0,[&]{return CompileObjectFeatureCatalog(bad);},[](const auto &){return false;},302);}
  const Json refs={{"slots",{{"ore",{{"block_names",Json::array({std::string(5001,'n'),42,"stone"})},{"fallback_slot","surface"}}},{"surface",Json::object()},{"ignored",42}}}};
  const auto refsOracle=[](const auto &value){return value && value->size()==2 && value->at("ore").BlockNames==std::vector<std::string>{std::string(5001,'n'),"stone"} &&
    value->at("ore").FallbackSlot=="surface" && value->at("surface").BlockNames==std::vector<std::string>{"surface"};};
  run("refs_names_fallback_and_filter",CatalogCandidateKind::Refs,refs,0,[&]{return CompileWorldGenRefsCatalog(refs);},refsOracle);
  {Json wide={{"slots",Json::object()}};for(int i=0;i<257;++i) wide["slots"]["slot"+std::to_string(i)]=Json::object();
    run("refs257_rehash",CatalogCandidateKind::Refs,wide,0,[&]{return CompileWorldGenRefsCatalog(wide);},[](const auto &v){return v && v->size()==257 && v->at("slot256").BlockNames==std::vector<std::string>{"slot256"};});}
  for(const auto &input:std::vector<Json>{Json{},Json::array(),Json{{"slots",Json::array()}},Json{{"slots",Json::object()}}})
    run("refs_missing_or_empty",CatalogCandidateKind::Refs,input,0,[&]{return CompileWorldGenRefsCatalog(input);},[](const auto &v){return !v;});
  const std::filesystem::path directory(std::wstring(5001,L'p'));
  Json manifest={{"id","candidate_pack"},{"name",std::string(8193,'n')},{"license",std::string(5001,'l')},{"min_game_version",std::string(129,'v')},
    {"depends",Json::array({"one",42,std::string(257,'d')})},{"conflicts",Json::array({"two",false})},{"worldgen_role","primary"},{"merge_mode","override"},
    {"version",7},{"pack_format",3},{"priority",-5},{"resolution",32},{"allow_resolution_mix",true}};
  const auto manifestOracle=[&](const auto &v){return v && v->Root==directory && v->Root.native().data()!=directory.native().data() && v->Id=="candidate_pack" &&
    v->Name==std::string(8193,'n') && v->License==std::string(5001,'l') && v->MinGameVersion==std::string(129,'v') &&
    v->Depends==std::vector<std::string>{"one",std::string(257,'d')} && v->Conflicts==std::vector<std::string>{"two"} && v->Role==WorldgenRole::Primary &&
    v->MergeMode==PackMergeMode::Override && v->Version==7 && v->PackFormat==3 && v->Priority==-5 && v->Resolution==32 && v->AllowResolutionMix;};
  run("manifest_fields_and_root",CatalogCandidateKind::Manifest,manifest,directory.native().size(),[&]{return CompileResourcePackManifest(manifest,directory);},manifestOracle);
  {Json empty=Json::object();run("manifest_filename_defaults",CatalogCandidateKind::Manifest,empty,directory.native().size(),[&]{return CompileResourcePackManifest(empty,directory);},[&](const auto &v){return v && v->Id==std::string(5001,'p') && v->Name==v->Id && v->Depends.empty() && v->Role==WorldgenRole::Secondary && v->MergeMode==PackMergeMode::SkipExisting;});}
  {Json bad=Json::array();run("manifest_root_rejection",CatalogCandidateKind::Manifest,bad,directory.native().size(),[&]{return CompileResourcePackManifest(bad,directory);},[](const auto &){return false;},306);}
  Json block={{"name",std::string(129,'b')},{"displayName",std::string(257,'d')},{"id",kPackBlockIdMin+7},{"hardness",3.5f},
    {"dig",{{"level",2},{"groups",{{"pickaxe",4},{"shovel",3}}}}},{"types",Json::array({std::string(257,'t'),42,"solid"})},
    {"render",{{"style","cross"}}},{"textures",Json::array()}};
  for(int i=0;i<6;++i) block["textures"].push_back(std::string(5001,char('a'+i)));
  const auto blockOracle=[](const auto &v){if(!v.Valid || v.Definition.Id!=kPackBlockIdMin+7 || v.Definition.Name!=std::string(129,'b') ||
    v.Definition.DisplayName!=std::string(257,'d') || v.Definition.Hardness!=3.5f || v.Definition.DigLevel!=2 || v.Definition.DigGroups.size()!=2 ||
    v.Definition.DigGroups.at("pickaxe")!=4 || v.Definition.DigGroups.at("shovel")!=3 || v.Definition.Types!=std::vector<std::string>{std::string(257,'t'),"solid"} ||
    v.Definition.Render.Style!=BlockRenderStyle::Cross || v.Definition.Physics.Movement.Occupancy!=0) return false;
    for(int i=0;i<6;++i) if(v.TextureStems[i]!=std::string(5001,char('a'+i))) return false;return true;};
  run("block_profiles_types_dig_textures",CatalogCandidateKind::Block,block,0,[&]{return ParseBlockFromJson(block,true);},blockOracle);
  {auto bad=block;bad["textures"][3]=42;run("block_invalid_texture",CatalogCandidateKind::Block,bad,0,[&]{return ParseBlockFromJson(bad,true);},[](const auto &v){return !v.Valid;});}
  {Json empty=Json::object();run("block_missing_name",CatalogCandidateKind::Block,empty,0,[&]{return ParseBlockFromJson(empty,true);},[](const auto &v){return !v.Valid;});}
  {
    std::optional<ParsedBlockJson> parsed;std::optional<ResourcePackBlock> adopted;
    {observation::Scope scope;parsed.emplace(ParseBlockFromJson(block,true));}
    const auto types=parsed->Definition.Types.data();const auto texture=parsed->TextureStems[0].data();
    const auto node=&*parsed->Definition.DigGroups.begin();observation::State.active=true;
    adopted.emplace(AdoptParsedResourceBlock(std::move(*parsed)));observation::State.active=false;
    const bool adopts=adopted->Definition.Types.data()==types && adopted->TextureStems[0].data()==texture && &*adopted->Definition.DigGroups.find(node->first)==node;
    report["resource_block_payload_adopted"]=adopts;if(!adopts) ++failures;adopted.reset();parsed.reset();
    if(observation::State.live || observation::State.blocks) ++failures;
  }
  const auto upper=BoundCatalogCandidateSource(CatalogCandidateKind::Features,MeasureCatalogCandidateInput(features)).requested_heap_upper;
  const auto overflow=BoundCatalogCandidateSource(CatalogCandidateKind::Manifest,{SIZE_MAX,SIZE_MAX,false},SIZE_MAX);
  report["overflow_rejected"]=overflow.overflow;if(!overflow.overflow) ++failures;
  {observation::Scope scope;void *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && observation::State.live==0;report["unmodeled_allocation_negative_control"]=negative?"PASS":"FAIL";if(!negative) ++failures;
  report["persistent_refusal_and_retry_cases"]=persistent;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Four shared production DOM-to-candidate stages including result move/shared/diagnostic allocations;input DOM/root/published owners/file/parser/CRT/platform terms separate. Full resource directory aggregation,WorldGenPack/prefab/merge candidates and publication graph still open.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
}

static int RunCatalogCandidatePublicationFixture(const char *reportPath,bool baseline)
{
  namespace fs=std::filesystem;
  Json report={{"schema","catalog-candidate-publication-refusal.v1"},{"status","PASS"},{"cases",Json::array()}};
  size_t failures=0;const auto parent=fs::temp_directory_path()/(L"cuba_pub_"+std::to_wstring(GetCurrentProcessId()));fs::create_directory(parent);
  const auto before=parent/L"before.json",after=parent/L"after.json";
  const auto write=[&](const fs::path &file,const char *name){std::ofstream out(file);out<<Json{{"vegetation",Json::array({{{"object",name},{"biomes",Json::array({"plains"})}}})}}.dump();};
  write(before,"before");write(after,"after");(void)UObjectFeatureTypeRegistry::Find("object");
  if(!UObjectFeatureConfigStorage::LoadFromFile(before)) return 1;
  {observation::Scope scope;if(!UObjectFeatureConfigStorage::LoadFromFile(after)) ++failures;}
  const auto normal=observation::State;auto current=UObjectFeatureConfigStorage::GetSnapshot();
  UObjectFeatureConfigStorage::LoadFromFile(before);current.reset();
  if(observation::State.live || observation::State.blocks) ++failures;
  const size_t begin=baseline?normal.attempts-1:0;
  bool reproduced=false;
  for(size_t i=begin;i<normal.attempts;++i) {
    const auto old=UObjectFeatureConfigStorage::GetSnapshot();bool refused=false;
    {observation::Scope scope(i);try{UObjectFeatureConfigStorage::LoadFromFile(after);}catch(const std::bad_alloc &){refused=true;}}
    current=UObjectFeatureConfigStorage::GetSnapshot();const bool replaced=current!=old;
    report["cases"].push_back({{"prefix",i},{"refused",refused},{"published_on_failure",replaced}});
    if(refused && replaced) reproduced=true;
    if(!baseline && (!refused || replaced)) ++failures;
    UObjectFeatureConfigStorage::LoadFromFile(before);current.reset();if(observation::State.live || observation::State.blocks) ++failures;
  }
  report["publication_after_refusal_reproduced"]=reproduced;report["normal_attempts"]=normal.attempts;
  if(baseline && !reproduced) ++failures;
  report["status"]=failures?"FAIL":baseline?"REPRODUCED":"PASS";report["failed_cases"]=failures;
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());
  if(resolved.parent_path()!=temp || resolved!=temp/parent.filename()) return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
