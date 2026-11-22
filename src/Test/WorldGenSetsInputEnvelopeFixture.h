#pragma once
#include "WorldGen/Core/WorldGenContentPin.h"
// Included only by the isolated ordinary-new observer executable, after its
// field oracle. Verification and input/model/report construction are outside
// the observed production operation. Existing outputs are destroyed before
// starting another epoch, including error-string capacity and partial results.
static ObjectFeatureRule EntryForComparison(const WorldGenObjectEntry &entry)
{
  ObjectFeatureRule r;r.ObjectName=entry.ObjectName;r.BiomeKeys=entry.Biomes;
  r.Spacing=entry.Spacing;r.Weight=entry.Weight;r.ChancePerColumn=entry.ChancePerColumn;
  r.SeedOffset=entry.SeedOffset;r.PlacementYOffset=entry.PlacementYOffset;r.Mode=entry.Mode;
  r.Scatter=entry.Scatter;r.Surface=entry.Surface;r.Tree=entry.Tree;r.Shrub=entry.Shrub;r.Ecology=entry.Ecology;
  return r;
}
static bool SameSets(const WorldGenSets &a,const WorldGenSets &b)
{
  const auto pool=[](const auto &left,const auto &right) {
    if(left.size()!=right.size()) return false;
    for(const auto &section:left) {
      const auto found=right.find(section.first);if(found==right.end() || section.second.Entries.size()!=found->second.Entries.size()) return false;
      for(size_t i=0;i<section.second.Entries.size();++i) {
        const auto &x=section.second.Entries[i],&y=found->second.Entries[i];
        if(x.SubBiomes!=y.SubBiomes || !SameRule(EntryForComparison(x),EntryForComparison(y))) return false;
      }
    }return true;
  };
  if(a.SchemaVersion!=b.SchemaVersion || !pool(a.VegetationSections,b.VegetationSections) ||
      !pool(a.GroundCoverSections,b.GroundCoverSections) || !pool(a.DecorationSections,b.DecorationSections) ||
      !pool(a.StructureSections,b.StructureSections) || a.Terrain.size()!=b.Terrain.size() || a.Ores.size()!=b.Ores.size()) return false;
  for(const auto &x:a.Terrain) {auto it=b.Terrain.find(x.first);if(it==b.Terrain.end() || x.second.Block!=it->second.Block) return false;}
  for(const auto &x:a.Ores) {auto it=b.Ores.find(x.first);if(it==b.Ores.end() || x.second.Enabled!=it->second.Enabled || x.second.DensityMultiplier!=it->second.DensityMultiplier) return false;}
  return true;
}
static int RunWorldGenSetsInputEnvelopeFixture(const char *reportPath)
{
  Json report={{"schema","worldgen-sets-input-construction-source.v1"},{"status","PASS"},
    {"parse",Json::array()},{"defaults",Json::array()},{"upgrade",Json::array()}};
  size_t failures=0,persistent=0;
  // Cold registry is a separately bounded process owner. Warm outside the
  // per-call cleanup observer so it is not mistaken for an escaped output.
  (void)UObjectFeatureTypeRegistry::Find("object");
  const auto countRow=[&](const char *kind,const std::string &name,const auto &bound,const auto &counts,bool ok) {
    ok=ok && bound.supported && !bound.overflow && counts.peak<=bound.requested_heap_upper && counts.live==0 && counts.blocks==0;
    if(!ok) ++failures;
    report[kind].push_back({{"name",name},{"pass",ok},{"source_upper",bound.requested_heap_upper},
      {"peak",counts.peak},{"max_request",counts.max_request},{"attempts",counts.attempts},{"final_live",counts.live}});
  };
  // Independently authored schema oracle, including all modes, ignored rows,
  // numeric normalization, array filtering, long names and optional groups.
  Json entries=Json::array();std::vector<WorldGenObjectEntry> expectedEntries;
  for(int mode=0;mode<4;++mode) {
    const char *names[]={"object","scatter_blocks","procedural_tree","procedural_shrub"};
    Json item={{"mode",names[mode]},{"spacing",17},{"weight",-7},{"chance_per_column",13},
      {"seed_offset",29},{"placement_y_offset",-4},{"biomes",Json::array({"plains",std::string(257,'k'),42})},
      {"sub_biomes",Json::array({"woodland","unknown",false})}};
    WorldGenObjectEntry entry;entry.Mode=static_cast<ObjectFeatureRuleMode>(mode);
    entry.Spacing=17;entry.Weight=1;entry.ChancePerColumn=13;entry.SeedOffset=29;entry.PlacementYOffset=-4;
    entry.Biomes={"plains",std::string(257,'k')};entry.SubBiomes={"woodland","unknown"};
    if(mode==0) {item["object"]="prefab";entry.ObjectName="prefab";}
    if(mode==1) {item["block"]="stone";item["attempts"]=0;item["radius"]=-1;entry.Scatter.BlockName="stone";entry.Scatter.Attempts=1;entry.Scatter.Radius=0;}
    if(mode==2) {item["species"]="tree";item["object"]=std::string(4097,'i');
      item["tree_profile"]={{"trunk_block",std::string(8193,'t')},{"height_min",10},{"height_max",7},{"crown_shape","conical"}};
      entry.ObjectName="tree";entry.Tree.TrunkBlock=std::string(8193,'t');entry.Tree.HeightMin=10;entry.Tree.HeightMax=10;entry.Tree.CrownShape=TreeCrownShape::Conical;}
    if(mode==3) {item["species"]="shrub";item["shrub_profile"]={{"accent_block",std::string(5001,'a')}};
      entry.ObjectName="shrub";entry.Shrub.AccentBlock=std::string(5001,'a');}
    entries.push_back(item);expectedEntries.push_back(entry);
  }
  entries.push_back({{"mode","unknown"}});entries.push_back({{"mode","object"}});
  Json full={{"schema_version",3},{"objects",Json::object()},
    {"terrain",{{"surface",{{"block",std::string(4097,'s')}}}}},
    {"ores",{{"copper",{{"enabled",false},{"density_multiplier",1.25f}}}}}};
  WorldGenSets expected;expected.Terrain["surface"].Block=std::string(4097,'s');expected.Ores["copper"]={false,1.25f};
  const char *pools[]={"vegetation","ground_cover","decoration","structures"};
  for(const char *name:pools) {full["objects"][name]["kept"]["entries"]=entries;full["objects"][name]["skipped"]={{"entries",nullptr}};}
  expected.VegetationSections["kept"].Entries=expectedEntries;expected.GroundCoverSections=expected.VegetationSections;
  expected.DecorationSections=expected.VegetationSections;expected.StructureSections=expected.VegetationSections;
  const auto parse=[&](const std::string &name,const Json &input,const WorldGenSets *oracle,bool expectedReturn,int expectedException,bool refusalMatrix=false) {
    const auto bound=BoundWorldGenSetsParseSource(input);WorldGenSets actual;std::string error;
    bool returned=false;int exception=0;
    {observation::Scope scope;try{returned=ParseWorldGenSets(input,actual,error);}catch(const Json::exception &e){exception=e.id;}}
    auto counts=observation::State;
    bool ok=returned==expectedReturn && exception==expectedException && (!oracle || SameSets(actual,*oracle));
    actual=WorldGenSets{};std::string().swap(error);counts.live=observation::State.live;counts.blocks=observation::State.blocks;
    countRow("parse",name,bound,counts,ok);
    if(refusalMatrix && ok) for(size_t index=0;index<counts.attempts;++index) {
      bool refused=false;
      {observation::Scope scope(index);try{ParseWorldGenSets(input,actual,error);}catch(const std::bad_alloc &){refused=true;}}
      actual=WorldGenSets{};std::string().swap(error);
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper) ++failures;
      {observation::Scope scope;returned=ParseWorldGenSets(input,actual,error);}
      if(!returned || !SameSets(actual,*oracle)) ++failures;
      actual=WorldGenSets{};std::string().swap(error);
      if(observation::State.live || observation::State.blocks) ++failures;
      ++persistent;
    }
  };
  for(int schema=1;schema<=3;++schema) {auto input=full;input["schema_version"]=schema;auto oracle=expected;oracle.SchemaVersion=schema;parse("full_schema"+std::to_string(schema),input,&oracle,true,0,schema==3);}
  {auto input=full;input["objects"]["vegetation"]["kept"]["entries"]=Json::array();
    auto oracle=expected;oracle.VegetationSections["kept"].Entries.clear();
    for(int i=0;i<257;++i) {input["objects"]["vegetation"]["kept"]["entries"].push_back(entries[i%4]);oracle.VegetationSections["kept"].Entries.push_back(expectedEntries[i%4]);}
    parse("wide257_partial_vector_growth",input,&oracle,true,0);}
  WorldGenSets empty;
  parse("non_object_null",nullptr,nullptr,false,0);parse("non_object_array",Json::array(),nullptr,false,0);
  parse("missing_schema",Json::object(),nullptr,false,0);
  parse("unsupported_schema",{{"schema_version",4}},nullptr,false,0);
  parse("wrong_schema_kind",{{"schema_version","one"}},nullptr,false,302);
  parse("wrong_objects_kind",{{"schema_version",3},{"objects",Json::array()}},&empty,true,0);
  parse("wrong_pool_kind",{{"schema_version",3},{"objects",{{"vegetation",Json::array()}}}},&empty,true,0);
  {auto input=full;input["objects"]["vegetation"]["kept"]["entries"][0]["mode"]=42;parse("wrong_mode_kind",input,nullptr,false,302);}
  {auto input=full;input["objects"]["vegetation"]["kept"]["entries"][0]["object"]=42;parse("wrong_object_name_kind",input,nullptr,false,302);}
  parse("wrong_terrain_slot_kind",{{"schema_version",3},{"terrain",{{"surface",42}}}},nullptr,false,306);
  parse("wrong_ore_slot_kind",{{"schema_version",3},{"ores",{{"copper",Json::array()}}}},nullptr,false,306);
  {
    Json input={{"schema_version",3},{"objects",Json::object()}};WorldGenSets oracle;
    for(int i=0;i<257;++i) {const auto key="section"+std::to_string(i);input["objects"]["vegetation"][key]["entries"]=Json::array({entries[0]});oracle.VegetationSections[key].Entries={expectedEntries[0]};}
    parse("map_rehash257_sections",input,&oracle,true,0);
  }
  {
    Json input={{"schema_version",3}},ignored=0;
    for(int i=0;i<4096;++i) {Json parent=Json::array();parent.push_back(std::move(ignored));ignored=std::move(parent);}
    input["ignored"]=std::move(ignored);parse("iterative_deep4096_ignored_domain",input,&empty,true,0);
  }

  // Default source is actual immutable storage. Loading its file/DOM is
  // deliberately outside this builder's allocation observer and acceptance.
  const auto defaults=[&](const std::string &name,const ObjectFeatureConfig &source,const WorldGenSets &oracle,bool refusalMatrix) {
    const auto bound=BoundDefaultWorldGenSetsSource(source);WorldGenSets actual;
    {observation::Scope scope;actual=BuildDefaultWorldGenSets();}
    auto counts=observation::State;bool ok=SameSets(actual,oracle);
    actual=WorldGenSets{};counts.live=observation::State.live;counts.blocks=observation::State.blocks;countRow("defaults",name,bound,counts,ok);
    if(refusalMatrix && ok) for(size_t index=0;index<counts.attempts;++index) {
      bool refused=false;{observation::Scope scope(index);try{actual=BuildDefaultWorldGenSets();}catch(const std::bad_alloc &){refused=true;}}
      actual=WorldGenSets{};
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper) ++failures;
      {observation::Scope scope;actual=BuildDefaultWorldGenSets();}
      if(!SameSets(actual,oracle)) ++failures;actual=WorldGenSets{};
      if(observation::State.live || observation::State.blocks) ++failures;++persistent;
    }
  };
  defaults("unloaded",ObjectFeatureConfig{},empty,false);
  const auto fixture=std::filesystem::temp_directory_path()/"cubatarium_sets_source_defaults.json";
  Json features;for(const char *name:pools) features[name]=entries;
  {std::ofstream file(fixture);file<<features.dump();}
  if(!UObjectFeatureConfigStorage::LoadFromFile(fixture)) {std::cerr<<"default source fixture load failed\n";return 1;}
  std::filesystem::remove(fixture);
  const auto source=UObjectFeatureConfigStorage::GetSnapshot();
  WorldGenSets defaultOracle;
  for(size_t i=0;i<4;++i) {auto e=expectedEntries[i];e.Weight=1;e.SubBiomes={"woodland","default"};defaultOracle.VegetationSections["default"].Entries.push_back(e);}
  defaultOracle.GroundCoverSections=defaultOracle.VegetationSections;defaultOracle.DecorationSections=defaultOracle.VegetationSections;defaultOracle.StructureSections=defaultOracle.VegetationSections;
  defaults("loaded_all4_modes",*source,defaultOracle,true);
  {
    auto enums=std::make_shared<ObjectFeatureConfig>();ObjectFeatureRule r;r.ObjectName="enum_source";
    r.Biomes={BiomeId::Plains,BiomeId::Invalid};r.SubBiomes={SubBiomeId::Woodland,SubBiomeId::Default};
    enums->Vegetation={r};WorldGenContentSnapshot snapshot;snapshot.Features=enums;
    UWorldGenContentPinScope pin(snapshot);WorldGenSets oracle;WorldGenObjectEntry e;e.ObjectName="enum_source";
    e.Biomes={"plains"};e.SubBiomes={"woodland","default"};oracle.VegetationSections["default"].Entries={e};
    oracle.GroundCoverSections["default"];oracle.DecorationSections["default"];oracle.StructureSections["default"];
    defaults("pinned_enum_keys_and_invalid_filter",*enums,oracle,false);
  }
  // Legacy upgrade has arbitrary caller spare capacity. Restore authored
  // profiles while preserving world frequencies/keys; every schema/profile.
  ObjectFeatureConfig authored;ObjectFeatureRule rule;rule.ObjectName="legacy";rule.Mode=ObjectFeatureRuleMode::ProceduralTree;
  rule.Tree.TrunkBlock=std::string(8193,'r');rule.Tree.LeavesBlock=std::string(5001,'l');rule.Shrub.AccentBlock=std::string(4097,'a');rule.Ecology.MinElevation=-51;
  authored.Vegetation={rule};
  for(int schema=1;schema<=3;++schema) for(int profile=1;profile<=2;++profile) {
    WorldGenSets actual;actual.SchemaVersion=schema;WorldGenObjectEntry entry;entry.ObjectName="legacy";entry.Spacing=39;entry.Weight=11;entry.Biomes={"custom"};
    actual.VegetationSections["legacy"].Entries={entry};auto &old=actual.VegetationSections["legacy"].Entries[0];
    old.Tree.TrunkBlock.reserve(256*1024);old.Tree.TrunkBlock="x";
    const auto bound=BoundWorldGenSetsUpgradeSource(actual,authored,profile);auto oracle=actual;
    if(profile>=2) oracle.SchemaVersion=3;
    if(schema==1 && profile>=2) {auto &e=oracle.VegetationSections["legacy"].Entries[0];e.Mode=rule.Mode;e.Tree=rule.Tree;e.Shrub=rule.Shrub;e.Ecology=rule.Ecology;e.Surface=rule.Surface;}
    const auto original=actual;
    {observation::Scope scope;UpgradeLegacyWorldGenSetMetadata(actual,authored,profile);}
    auto counts=observation::State;bool ok=SameSets(actual,oracle);actual=WorldGenSets{};
    counts.live=observation::State.live;counts.blocks=observation::State.blocks;
    countRow("upgrade","schema"+std::to_string(schema)+"_profile"+std::to_string(profile),bound,counts,ok);
    if(schema==1 && profile==2 && ok) for(size_t index=0;index<counts.attempts;++index) {
      actual=original;actual.VegetationSections["legacy"].Entries[0].Tree.TrunkBlock.reserve(256*1024);
      bool refused=false;{observation::Scope scope(index);try{UpgradeLegacyWorldGenSetMetadata(actual,authored,profile);}catch(const std::bad_alloc &){refused=true;}}
      actual=WorldGenSets{};
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper) ++failures;
      actual=original;actual.VegetationSections["legacy"].Entries[0].Tree.TrunkBlock.reserve(256*1024);
      {observation::Scope scope;UpgradeLegacyWorldGenSetMetadata(actual,authored,profile);}
      if(!SameSets(actual,oracle)) ++failures;actual=WorldGenSets{};
      if(observation::State.live || observation::State.blocks) ++failures;++persistent;
    }
  }
  report["persistent_refusal_and_retry_cases"]=persistent;
  const auto bound=BoundWorldGenSetsParseSource(full);
  {observation::Scope scope;void *extra=::operator new(bound.requested_heap_upper+1);::operator delete(extra);}
  const bool detected=observation::State.peak>bound.requested_heap_upper && observation::State.live==0;
  report["unmodeled_allocation_negative_control"]=detected?"PASS":"FAIL";if(!detected) ++failures;
  report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="DOM-to-sets/default/legacy upgrade ordinary-new requests;input DOM/previous output capacities/global source ownership separate;partial exception outputs destroyed;no publication rollback,file/CRT/stack/process-cap acceptance";
  if(reportPath) {std::ofstream file(reportPath);file<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
}
