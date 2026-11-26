#pragma once
#include "Test/CatalogBoundProfileFixture.h"
#include <sstream>
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "WorldGen/Core/WorldGenSetsSourceEnvelope.h"
#include "WorldGen/Core/SourceInputStorageCensus.h"

namespace settings_composition_fixture
{
using namespace cutum;using Json=nlohmann::json;
inline void Write(const fs::path &path,const Json &data){fs::create_directories(path.parent_path());std::ofstream out(path);out<<data.dump();}
inline Json Features(bool old)
{
  const auto label=old?"old":"new";
  return {{"vegetation",Json::array({{{"mode","procedural_tree"},{"species","tree_shared"},{"biomes",Json::array({"plains"})},
      {"tree_profile",{{"trunk_block",std::string(old?8193:5001,old?'o':'n')},{"leaves_block",std::string(old?4097:2049,old?'l':'s')}}}}})},
      {"ground_cover",Json::array({{{"object",std::string("ground_")+label},{"biomes",Json::array({"plains"})}}})},
      {"decoration",Json::array({{{"object",std::string("decor_")+label},{"biomes",Json::array({"plains"})}}})},
      {"structures",Json::array({{{"object",std::string("structure_")+label},{"biomes",Json::array({"plains"})}}})},
      {"cave_features",Json::array({{{"biome","lush_caves"},{"kind","moss_patch"},{"block",std::string("cave_")+label}}})}};
}
inline CatalogFileSourceDomain FileDomain(const fs::path &path,CatalogCandidateKind kind,size_t rootUnits=0)
{
  return {kind,static_cast<size_t>(fs::file_size(path)),path.native().size(),rootUnits,0};
}
inline DefinitionDirectorySourceDomain DirectoryDomain(const fs::path &path)
{
  DefinitionDirectorySourceDomain d;d.max_path_units=path.native().size();
  for(const auto &e:fs::directory_iterator(path)){d.max_path_units=std::max(d.max_path_units,e.path().native().size());if(e.path().extension()!=".json")continue;
    const auto f=BoundCatalogFileSource(FileDomain(e.path(),CatalogCandidateKind::Block));++d.files;detail::CandidateAdd(d.file_source_sum,f.requested_heap_upper,d.overflow);d.supported=d.supported && f.supported;d.overflow=d.overflow || f.overflow;}
  return d;
}
inline bool FeatureValue(const std::shared_ptr<const ObjectFeatureConfig> &value,bool old)
{
  return value && value->Vegetation.size()==1 && value->GroundCover.size()==1 && value->Decoration.size()==1 && value->Structures.size()==1 &&
      value->Vegetation[0].Mode==ObjectFeatureRuleMode::ProceduralTree && value->Vegetation[0].ObjectName=="tree_shared" &&
      value->Vegetation[0].Tree.TrunkBlock==std::string(old?8193:5001,old?'o':'n') && value->GroundCover[0].ObjectName==(old?"ground_old":"ground_new");
}
inline int Run(const char *path,bool owner_binding=false)
{
  const auto root=fs::path("build/desktop-msvc/s4-settings-composition-fixtures");
  for(bool old:{true,false}){const auto dir=root/(old?"old":"new");
    Write(dir/"features.json",Features(old));Write(dir/"refs.json",{{"slots",{{"surface",{{"block_names",Json::array({old?"old_stone":"new_stone",std::string(257,old?'o':'n')})},{"fallback_slot","fallback"}}},{"fallback",{{"block_names",Json::array({"stone"})}}}}}});
    Write(dir/"pack.json",{{"id",old?"manifest_old":"manifest_new"},{"name",std::string(129,old?'o':'n')},{"version",old?1:2},{"license",std::string(4097,'l')}});
    Write(dir/"definitions/block.json",{{"name",old?"old_block":"new_block"},{"id",old?548:549},{"types",Json::array({"solid",std::string(129,old?'o':'n')})}});
  }
  Write(root/"reset_features.json",Json::object());Write(root/"reset_refs.json",{{"slots",{{"reset",{{"block_names",Json::array({"stone"})}}}}}});
  const auto reset=[&]{const bool a=UObjectFeatureConfigStorage::LoadFromFile(root/"reset_features.json"),b=UWorldGenRefs::LoadFromFile(root/"reset_refs.json");(void)UObjectFeatureConfigStorage::Get();return a && b;};
  // Exercise the same real paths before the observed epoch so read-only mode/
  // enum registries and persistent stream locale initialization are not output.
  (void)UObjectFeatureTypeRegistry::Find("object");(void)BiomeIdFromString("plains");(void)SubBiomeIdFromString("woodland");
  UObjectFeatureConfigStorage::LoadFromFile(root/"old/features.json");UWorldGenRefs::LoadFromFile(root/"old/refs.json");(void)reset();
  Json authored={{"schema_version",1},{"objects",{{"vegetation",{{"world",{{"entries",Json::array({{{"mode","object"},{"object","tree_shared"},{"spacing",37},{"weight",7},{"chance_per_column",13},{"biomes",Json::array({"custom_biome"})},{"sub_biomes",Json::array({"woodland"})}}})}}}}}}},{"ignored","spare_input"}};
  std::optional<Json> document;std::optional<WorldGenSets> parsed,defaults;std::optional<ObjectFeatureConfig> resolved;
  std::optional<CatalogJsonDocument> managed;std::istringstream managedInput(R"({"managed":{"nested":[[{"value":"arena"}]]}})");
  std::optional<ResourcePackManifest> oldManifest,currentManifest;std::string error;
  std::array<WorldGenContentSnapshot,4> snapshots{};std::shared_ptr<UBlockDefinitionStorage> definitions;std::array<std::shared_ptr<const BlockDefinitionCatalog>,2> blocks{};
  Json report={{"schema","settings-file-construction-composition.v1"},{"phases",Json::array()}};size_t failures=0;
  {observation::Scope scope;document.emplace(authored);(*document)["ignored"].get_ref<std::string&>().reserve(256*1024);parsed.emplace();defaults.emplace();
    if(!UObjectFeatureConfigStorage::LoadFromFile(root/"old/features.json") || !UWorldGenRefs::LoadFromFile(root/"old/refs.json"))return 1;
    snapshots[0].Features=UObjectFeatureConfigStorage::GetSnapshot();snapshots[0].Refs=UWorldGenRefs::GetSnapshot();
    oldManifest=UResourcePack::LoadManifest(root/"old");definitions=std::make_shared<UBlockDefinitionStorage>();definitions->Load((root/"old/definitions").string());blocks[0]=definitions->GetCatalogSnapshot();
    if(owner_binding)managed.emplace(ParseCatalogSourceDocument(managedInput,CatalogJsonKind::Features));}
  snapshots[1]=snapshots[0];blocks[1]=blocks[0];
  const auto inputCensus=[&]{const WorldGenSets *sets[]={parsed?&*parsed:nullptr,defaults?&*defaults:nullptr,parsed?&*parsed:nullptr};
    const Json *docs[]={document?&*document:nullptr,document?&document->at("objects"):nullptr,document?&*document:nullptr,
      managed?static_cast<const Json*>(&*managed):nullptr,managed?&managed->at("managed"):nullptr};return MeasureSourceInputStorage(sets,3,docs,5);};
  const auto census=[&]{snapshots[2]={};if(resolved)snapshots[2].Features=std::shared_ptr<const ObjectFeatureConfig>(std::shared_ptr<const ObjectFeatureConfig>{},&*resolved);
    snapshots[3]={};snapshots[3].Features=UObjectFeatureConfigStorage::GetThreadReadSnapshot();CatalogResourceRoots roots;roots.block_storage=definitions;
    const auto c=MeasureCatalogStorage(snapshots.data(),snapshots.size(),blocks.data(),blocks.size(),{},roots);auto entry=BoundRetainedCatalogRequests(c);
    const auto inputs=inputCensus();entry=JoinCatalogOwnerInputs(entry,inputs,managed?managed->RetirementRequestedBytes():0);
    detail::CatalogStorageInspector extra;if(oldManifest)extra.Manifest(*oldManifest);if(currentManifest)extra.Manifest(*currentManifest);extra.Text(error);
    detail::CandidateAdd(entry.requested_heap_upper,detail::MergeCensusUpper(extra.Result,entry.overflow),entry.overflow);return entry;};
  catalog_bound_profile_fixture::Profile profile(census());
  const auto phase=[&](const char *name,const auto &source,auto operation){const auto entry=census();const auto bound=ComposeCatalogConstruction(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto ownerBound=profile.Phase(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto entryObserved=observation::State.live;observation::State.peak=observation::State.live;observation::State.active=true;const bool completed=operation();observation::State.active=false;
    const auto peak=observation::State.peak;const bool ok=completed && bound.supported && !bound.overflow && entryObserved<=entry.requested_heap_upper && peak<=bound.requested_heap_upper &&
      (!owner_binding || (ownerBound.supported && !ownerBound.overflow && peak<=ownerBound.requested_heap_upper));
    if(!ok)++failures;report["phases"].push_back({{"name",name},{"pass",ok},{"entry_observed",entryObserved},{"entry_upper",entry.requested_heap_upper},{"peak",peak},{"upper",bound.requested_heap_upper},{"owner_profile_upper",ownerBound.requested_heap_upper}});};
  phase("feature_file_publication",BoundCatalogFileSource(FileDomain(root/"new/features.json",CatalogCandidateKind::Features)),[&]{const bool result=UObjectFeatureConfigStorage::LoadFromFile(root/"new/features.json");snapshots[1].Features=UObjectFeatureConfigStorage::GetSnapshot();return result;});
  phase("refs_file_publication",BoundCatalogFileSource(FileDomain(root/"new/refs.json",CatalogCandidateKind::Refs)),[&]{const bool result=UWorldGenRefs::LoadFromFile(root/"new/refs.json");snapshots[1].Refs=UWorldGenRefs::GetSnapshot();return result;});
  phase("manifest_file_result",BoundCatalogFileSource(FileDomain(root/"new/pack.json",CatalogCandidateKind::Manifest,(root/"new").native().size())),[&]{currentManifest=UResourcePack::LoadManifest(root/"new");return bool(currentManifest);});
  phase("definition_directory_publication",BoundDefinitionDirectorySource(DirectoryDomain(root/"new/definitions")),[&]{definitions->Load((root/"new/definitions").string());blocks[1]=definitions->GetCatalogSnapshot();return true;});
  phase("preparsed_settings",BoundWorldGenSetsParseSource(*document),[&]{return ParseWorldGenSets(*document,*parsed,error);});
  phase("default_settings",BoundDefaultWorldGenSetsSource(*snapshots[1].Features),[&]{*defaults=BuildDefaultWorldGenSets();return true;});
  phase("legacy_settings_upgrade",BoundWorldGenSetsUpgradeSource(*parsed,*snapshots[1].Features,2),[&]{UpgradeLegacyWorldGenSetMetadata(*parsed,*snapshots[1].Features,2);return true;});
  phase("resolved_features",BoundResolvedFeaturesSource(*parsed,*snapshots[1].Features,2),[&]{resolved.emplace(ResolveObjectFeatures(*parsed,*snapshots[1].Features,2));return true;});
  const auto &e=parsed->VegetationSections.at("world").Entries[0];const auto &r=resolved->Vegetation[0];
  const bool values=FeatureValue(snapshots[0].Features,true) && FeatureValue(snapshots[1].Features,false) &&
      snapshots[0].Refs->at("surface").BlockNames[0]=="old_stone" && snapshots[1].Refs->at("surface").BlockNames[0]=="new_stone" &&
      oldManifest && oldManifest->Id=="manifest_old" && currentManifest && currentManifest->Id=="manifest_new" &&
      blocks[0]->ById.at(548).Name=="old_block" && blocks[1]->ById.at(549).Types[1]==std::string(129,'n') &&
      parsed->SchemaVersion==3 && e.ObjectName=="tree_shared" && e.Spacing==37 && e.Weight==7 && e.Mode==ObjectFeatureRuleMode::ProceduralTree && e.Tree.TrunkBlock==std::string(5001,'n') &&
      defaults->VegetationSections.at("default").Entries[0].Tree.TrunkBlock==std::string(5001,'n') &&
      resolved->Vegetation.size()==1 && resolved->GroundCover.size()==1 && resolved->Decoration.size()==1 && resolved->Structures.size()==1 &&
      r.Mode==ObjectFeatureRuleMode::ProceduralTree && r.Tree.TrunkBlock==std::string(5001,'n') && r.Spacing==37 && r.Weight==7 && r.BiomeKeys==std::vector<std::string>{"custom_biome"};
  if(!values)++failures;
  const auto input=inputCensus();const WorldGenSets *uniqueSets[]={&*parsed,&*defaults};const Json *uniqueDoc[]={&*document,managed?static_cast<const Json*>(&*managed):nullptr};const auto unique=MeasureSourceInputStorage(uniqueSets,2,uniqueDoc,2);
  const bool inputDedup=input.RequestedHeapComponents()==unique.RequestedHeapComponents();const auto entry=census();
  if(!inputDedup || observation::State.live>entry.requested_heap_upper)++failures;
  report["retained"]={{"observed",observation::State.live},{"upper",entry.requested_heap_upper},{"input_heap",input.RequestedHeapComponents()},{"input_alias_deduplicated",inputDedup},{"ignored_spare_capacity",(*document)["ignored"].get_ref<const std::string&>().capacity()}};
  report["old_current_oracles"]=values;
  if(owner_binding){auto checks=profile.Controls(entry);const auto arena=managed->RetirementRequestedBytes();
    const auto overflowArena=JoinCatalogOwnerInputs(entry,input,SIZE_MAX);
    const bool missingArena=!JoinCatalogOwnerInputs(entry,input,arena,false).supported;
    checks["managed_arena_requests"]=arena;checks["arena_overflow_rejected"]=overflowArena.overflow;checks["unknown_arena_profile_rejected"]=missingArena;
    const bool ok=checks["pass"].get<bool>() && arena>0 && overflowArena.overflow && missingArena;if(!ok)++failures;checks["pass"]=ok;report["owner_profile_binding"]=checks;}
  snapshots={};blocks={};definitions.reset();document.reset();managed.reset();parsed.reset();defaults.reset();resolved.reset();oldManifest.reset();currentManifest.reset();std::string{}.swap(error);
  if(!reset())return 1;const bool graphZero=!observation::State.live && !observation::State.blocks;if(!graphZero)++failures;report["graph_final_zero"]=graphZero;
  if(!graphZero){report["remaining_live"]=observation::State.live;report["status"]="FAIL";if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}return 1;}
  // Real file boundaries,including path/log/publication work and exact retries.
  size_t prefixes=0;report["file_refusals"]=Json::array();
  for(int kind=0;kind<4;++kind){const auto inputPath=kind==0?root/"new/features.json":kind==1?root/"new/refs.json":kind==2?root/"new/pack.json":root/"new/definitions";
    const auto b=kind<3?BoundCatalogFileSource(FileDomain(inputPath,kind==0?CatalogCandidateKind::Features:kind==1?CatalogCandidateKind::Refs:CatalogCandidateKind::Manifest,(root/"new").native().size())):BoundDefinitionDirectorySource(DirectoryDomain(inputPath));
    const auto prepare=[&]{(void)reset();if(kind==3){definitions=std::make_shared<UBlockDefinitionStorage>();definitions->Load((root/"old/definitions").string());}};
    const auto invoke=[&]{if(kind==0)return UObjectFeatureConfigStorage::LoadFromFile(inputPath);if(kind==1)return UWorldGenRefs::LoadFromFile(inputPath);if(kind==2){currentManifest=UResourcePack::LoadManifest(root/"new");return bool(currentManifest);}definitions->Load(inputPath.string());return true;};
    const auto matches=[&]{if(kind==0)return FeatureValue(UObjectFeatureConfigStorage::GetSnapshot(),false);if(kind==1)return UWorldGenRefs::GetSnapshot()->at("surface").BlockNames[0]=="new_stone";if(kind==2)return currentManifest && currentManifest->Id=="manifest_new";return definitions->GetCatalogSnapshot()->ById.at(549).Types[1]==std::string(129,'n');};
    const auto clean=[&]{currentManifest.reset();definitions.reset();(void)reset();return !observation::State.live && !observation::State.blocks;};
    prepare();bool returned=false;{observation::Scope scope;returned=invoke();}const auto normal=observation::State;const bool ok=returned && matches() && normal.peak<=b.requested_heap_upper;const bool cleaned=clean();if(!ok || !cleaned)++failures;
    report["file_refusals"].push_back({{"kind",kind},{"pass",ok && cleaned},{"attempts",normal.attempts},{"peak",normal.peak},{"upper",b.requested_heap_upper}});
    if(ok && cleaned)for(size_t i=0;i<normal.attempts;++i){prepare();const auto oldFeature=UObjectFeatureConfigStorage::GetSnapshot();const auto oldRefs=UWorldGenRefs::GetSnapshot();const auto oldBlocks=definitions?definitions->GetCatalogSnapshot():nullptr;bool threw=false;returned=false;
      {observation::Scope scope(i);try{returned=invoke();}catch(const std::bad_alloc &){threw=true;}}
      const auto failed=observation::State;
      if(!failed.refusals || failed.peak>b.requested_heap_upper || ((threw || !returned) && ((kind==0 && UObjectFeatureConfigStorage::GetSnapshot()!=oldFeature) || (kind==1 && UWorldGenRefs::GetSnapshot()!=oldRefs) || (kind==3 && definitions->GetCatalogSnapshot()!=oldBlocks))))++failures;
      if(!clean()){++failures;break;}
      prepare();{observation::Scope scope;returned=invoke();}if(!returned || !matches())++failures;if(!clean()){++failures;break;}++prefixes;
    }
  }
  const auto overflow=BoundCatalogFileSource({CatalogCandidateKind::Features,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX});const auto dirOverflow=BoundDefinitionDirectorySource({SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,true,false});
  if(!overflow.overflow || !dirOverflow.overflow)++failures;
  const auto source=BoundCatalogFileSource({CatalogCandidateKind::Features,0,0,0,0});{observation::Scope scope;auto *extra=::operator new(source.requested_heap_upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>source.requested_heap_upper && !observation::State.live;if(!negative)++failures;
  report["file_refusal_and_exact_retry_cases"]=prefixes;report["overflow_rejected"]=overflow.overflow && dirOverflow.overflow;report["negative_control"]=negative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Eight actual file/settings phases with supplied old/current/global/TLS and returned rules,preparsed DOM/settings capacities deduplicated independently. Standard warmed MSVC Release/default files. No prefab/legacy cube/atlas/all-world/thread/CRT/OS/GPU/process acceptance;input inspector scratch and native exception objects separate.";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace settings_composition_fixture
