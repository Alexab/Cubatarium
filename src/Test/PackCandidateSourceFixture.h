#pragma once
#include "WorldGen/Core/PackCandidateSourceEnvelope.h"
static int RunPackCandidateSourceFixture(const char *reportPath)
{
  Json report={{"schema","pack-document-source.v1"},{"cases",Json::array()}};
  size_t failures=0,prefixes=0;const auto active=UWorldGenPack::GetSnapshot();
  const auto run=[&](const char *name,const Json &input,auto produce,auto oracle,bool rejected=false){
    const auto bound=BoundPackCandidateSource(MeasureCatalogCandidateInput(input),257);
    using T=decltype(produce());std::optional<T> candidate;bool error=false;
    {observation::Scope scope;try{candidate.emplace(produce());}catch(const std::exception &){error=true;}}
    const auto normal=observation::State;
    const bool value=error==rejected && (rejected || (candidate && oracle(*candidate)));candidate.reset();
    const bool ok=value && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts},{"rejected",error}});
    if(ok && !rejected)for(size_t i=0;i<normal.attempts;++i){
      bool refused=false;{observation::Scope scope(i);try{candidate.emplace(produce());}catch(const std::bad_alloc &){refused=true;}}
      candidate.reset();if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      {observation::Scope scope;candidate.emplace(produce());}if(!oracle(*candidate))++failures;candidate.reset();
      if(observation::State.live || observation::State.blocks || UWorldGenPack::GetSnapshot()!=active)++failures;++prefixes;
    }
  };
  const auto document=[&](const char *name,WorldGenPackDocument kind,const Json &input,auto oracle,bool rejected=false){
    run(name,input,[&]{auto result=std::make_shared<WorldGenPack>();ApplyWorldGenPackDocument(kind,input,*result);return result;},[&](const auto &v){return oracle(*v);},rejected);
  };
  Json root={{"id",std::string(5001,'i')},{"biome_mode","image"},{"biome_map_image",std::string(8193,'p')},{"biome_map_block_scale",0},{"biome_blend_radius",3.5f},{"pipeline",Json::array({"caves","terrain","unknown",42,"ores","caves"})}};
  document("root_fields",WorldGenPackDocument::Root,root,[](const auto &v){return v.Id==std::string(5001,'i') && v.BiomeMapImagePath==std::string(8193,'p') && v.BiomeMode==WorldGenBiomeMode::Image && v.BiomeMapBlockScale==1 && v.BiomeBlendRadius==3.5f && v.Pipeline.Loaded && v.Pipeline.Caves && v.Pipeline.Ores && v.Pipeline.StageOrder.size()==3;});
  document("root_defaults",WorldGenPackDocument::Root,Json::object(),[](const auto &v){return v.Id=="default" && v.BiomeMode==WorldGenBiomeMode::Procedural && !v.Pipeline.Loaded;});
  Json pipeline={{"stages",Json::array()}};for(int i=0;i<257;++i)pipeline["stages"].push_back(i%2?"ores":"caves");
  document("pipeline257_growth",WorldGenPackDocument::Pipeline,pipeline,[](const auto &v){return v.Pipeline.Loaded && v.Pipeline.Caves && v.Pipeline.StageOrder.size()==257;});
  Json ores={{"ores",Json::array()}};for(int i=0;i<129;++i)ores["ores"].push_back({{"slot",std::string(257,'s')+std::to_string(i)},{"y_peak",0},{"vein_size",100},{"rarity",2.0f}});
  document("ores129_growth",WorldGenPackDocument::Ores,ores,[](const auto &v){return v.Ores.Loaded && v.Ores.Rules.size()==129 && v.Ores.Rules.back().Slot==std::string(257,'s')+"128" && v.Ores.Rules[0].YPeak==1 && v.Ores.Rules[0].VeinSize==32 && v.Ores.Rules[0].Rarity==1;});
  document("height",WorldGenPackDocument::Height,Json{{"overworld",{{"sea_bias",0.37f}}}},[](const auto &v){return v.Height.Loaded && v.Height.SeaBias==0.37f;});
  document("climate",WorldGenPackDocument::Climate,Json{{"temperature",{{"scale",0.12f}}},{"temperature_lapse_per_block",1.0f}},[](const auto &v){return v.Climate.Loaded && v.Climate.Temperature.Scale==0.12f && v.Climate.TemperatureLapsePerBlock==0.004f;});
  document("valleys",WorldGenPackDocument::Valleys,Json{{"max_depth",27}},[](const auto &v){return v.Valleys.Loaded && v.Valleys.MaxDepth==27;});
  document("lakes",WorldGenPackDocument::Lakes,Json{{"cell_size",1}},[](const auto &v){return v.Lakes.Loaded && v.Lakes.CellSize==48;});
  document("ravines",WorldGenPackDocument::Ravines,Json{{"rarity",17}},[](const auto &v){return v.Ravines.Loaded && v.Ravines.Rarity==17;});
  document("caves",WorldGenPackDocument::Caves,Json{{"max_depth_below_surface",71}},[](const auto &v){return v.Caves.Loaded && v.Caves.MaxDepthBelowSurface==71;});
  document("aquifers",WorldGenPackDocument::Aquifers,Json{{"cell_size_x",21}},[](const auto &v){return v.Aquifers.Loaded && v.Aquifers.CellSizeX==21;});
  document("wrong_root",WorldGenPackDocument::Root,Json::array(),[](const auto &){return false;},true);
  Json biome={{"schema_version",3},{"domain","cave"},{"climate",{{"temperature",Json::array({0.2f,0.8f})},{"weight",3.0f}}},
    {"height",{{"base_offset",-7.0f}}},{"palette",{{"surface",std::string(5001,'s')}}},
    {"features",{{"decoration",{{std::string(257,'f'),4.0f}}}}},
    {"sub_biomes",{{"woodland",{{"subsurface",std::string(129,'u')},{"vegetation_weight_mul",2.0f}}}}},
    {"weather",{{"temperature_offset",0.25f},{"precipitation_multiplier",2.0f},{"wind_exposure",1.5f}}}};
  run("biome_authored",biome,[&]{return CompileWorldGenBiomeCatalog(biome,"custom_cave");},[](const auto &v){return v.SchemaVersion==3 && v.Domain==BiomeDomain::Cave && v.Climate.Enabled && v.Climate.Temperature.Min==0.2f && v.Climate.Temperature.Max==0.8f && v.Climate.Weight==3 && v.Height.baseOffsetBlocks==-7 && v.SurfaceSlot==std::string(5001,'s') && v.FeatureWeights.at(std::string(257,'f'))==4 && v.SubBiomes.at("woodland").SubsurfaceSlot==std::string(129,'u') && v.SubBiomes.at("woodland").VegetationWeightMul==2 && v.Weather.Defined && v.Weather.TemperatureOffset==0.25f && v.Weather.PrecipitationMultiplier==2 && v.Weather.WindExposure==1.5f;});
  auto wide=biome;wide["features"]["decoration"]=Json::object();wide["sub_biomes"]=Json::object();
  for(int i=0;i<129;++i){wide["features"]["decoration"]["feature"+std::to_string(i)]=2;wide["sub_biomes"]["sub"+std::to_string(i)]={{"subsurface",std::string(129,'x')}};}
  run("biome129_map_rehash",wide,[&]{return CompileWorldGenBiomeCatalog(wide,"custom_cave");},[](const auto &v){return v.FeatureWeights.size()==129 && v.FeatureWeights.at("feature128")==2 && v.SubBiomes.size()==129 && v.SubBiomes.at("sub128").SubsurfaceSlot==std::string(129,'x');});
  const Json empty=Json::object();run("builtin_v1_defaults",empty,[&]{return CompileWorldGenBiomeCatalog(empty,"lush_caves");},[](const auto &v){return v.SchemaVersion==1 && v.Domain==BiomeDomain::Cave && !v.Climate.Enabled;});
  run("custom_requires_v2",empty,[&]{return CompileWorldGenBiomeCatalog(empty,"custom");},[](const auto &){return false;},true);
  auto bad=biome;bad["climate"]["temperature"]=Json::array({0.8f,0.2f});run("invalid_climate",bad,[&]{return CompileWorldGenBiomeCatalog(bad,"custom");},[](const auto &){return false;},true);
  bad=biome;bad["features"][std::string(5001,'q')]=Json::object();run("long_unknown_pool",bad,[&]{return CompileWorldGenBiomeCatalog(bad,"custom");},[](const auto &){return false;},true);
  const auto overflow=BoundPackCandidateSource({SIZE_MAX,SIZE_MAX,false},SIZE_MAX);if(!overflow.overflow)++failures;
  const auto upper=BoundPackCandidateSource(MeasureCatalogCandidateInput(root)).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;report["persistent_prefix_and_retry_cases"]=prefixes;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Fresh pack per document/all10 kinds and biome conversion;actual shared production code,input DOM/old capacities/directory registry finalization/image/publication graph separate. Optional-file wrapper catch/partial mutation behavior preserved,not transactional directory loading.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
