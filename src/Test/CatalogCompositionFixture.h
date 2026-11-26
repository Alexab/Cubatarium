#pragma once
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "Test/StbiAllocationFixture.h"
#include "WorldGen/Core/CatalogRuntimeDomain.h"
#include "Test/CatalogOwnerCompositionControls.h"

namespace catalog_composition_fixture
{
using namespace cutum;using Json=nlohmann::json;
inline void Write(const fs::path &path,const Json &value){fs::create_directories(path.parent_path());std::ofstream out(path);out<<value.dump();}
inline void Pack(const fs::path &root,bool old)
{
  Write(root/"pack.json",{{"id",old?"old_composition":"new_composition"},{"biome_mode","image"},{"biome_map_image",old?"map.ppm":"map.png"}});
  Write(root/"pipeline.json",{{"stages",Json::array({"caves","ores"})}});
  Write(root/"height.json",{{"overworld",{{"sea_bias",0.37f}}}});
  Write(root/"climate.json",{{"temperature",{{"scale",0.12f}}}});
  Write(root/"ores.json",{{"ores",Json::array({{{"slot","test_ore"},{"y_peak",23}}})}});
  Write(root/"caves.json",{{"max_depth_below_surface",71}});Write(root/"aquifers.json",{{"cell_size_x",21}});
  Write(root/"ravines.json",{{"rarity",17}});Write(root/"valleys.json",{{"max_depth",27}});Write(root/"lakes.json",{{"cell_size",48}});
  for(bool cave:{false,true})Write(root/"biomes"/(cave?"cave.json":"surface.json"),{{"id",cave?"composition_cave":"composition_surface"},
      {"schema_version",2},{"domain",cave?"cave":"surface"},{"climate",{{"temperature",Json::array({0.2f,0.8f})}}},
      {"palette",{{"surface",std::string(257,'s')}}},{"sub_biomes",{{"woodland",{{"subsurface",std::string(129,'u')}}}}}});
  if(old){std::ofstream out(root/"map.ppm",std::ios::binary);out<<"P6\n2 2\n255\n";const unsigned char pixels[]={0x2d,0x8a,0x3f,0xe8,0xd4,0x6a,0x7a,0x7a,0x7a,0xd8,0xe8,0xf0};out.write(reinterpret_cast<const char *>(pixels),sizeof(pixels));}
  else {const auto png=test::MakeStbiAllocationPng();std::ofstream out(root/"map.png",std::ios::binary);out.write(reinterpret_cast<const char *>(png.data()),png.size());}
}
inline PackDirectorySourceDomain Inventory(const fs::path &root,bool image)
{
  PackDirectorySourceDomain d;d.root_input_bytes=root.string().size();d.max_path_units=root.native().size()+32;d.decode_image=image;
  d.image={StbiSourceFormat::Png,64,64,3,16384,64*64*3+64};
  const auto read=[&](const fs::path &p,bool biome){if(!fs::exists(p))return;d.max_path_units=std::max(d.max_path_units,p.native().size());
    std::ifstream in(p,std::ios::binary);const std::string bytes(std::istreambuf_iterator<char>(in),{});AddPackDirectoryDocument(d,bytes.size(),biome);};
  for(const auto *name:{"pack.json","pipeline.json","height.json","climate.json","ores.json","caves.json","aquifers.json","ravines.json","valleys.json","lakes.json"})read(root/name,false);
  if(fs::exists(root/"biomes"))for(const auto &entry:fs::directory_iterator(root/"biomes")){d.max_path_units=std::max(d.max_path_units,entry.path().native().size());if(entry.is_regular_file() && entry.path().extension()==".json")read(entry.path(),true);}
  return d;
}
inline bool NewPack(const std::shared_ptr<const WorldGenPack> &p)
{
  if(!p || p->Id!="new_composition" || p->Biomes.size()!=2 || !p->BiomeRegistry || p->BiomeRegistry->Size()!=19 ||
      p->SurfaceClimateBiomes.size()!=1 || p->CaveClimateBiomes.size()!=1 || !p->Pipeline.Loaded || p->Pipeline.StageOrder.size()!=2 ||
      !p->Height.Loaded || p->Height.SeaBias!=0.37f || !p->Climate.Loaded || p->Climate.Temperature.Scale!=0.12f ||
      !p->Ores.Loaded || p->Ores.Rules.size()!=1 || p->Ores.Rules[0].Slot!="test_ore" || !p->Caves.Loaded || p->Caves.MaxDepthBelowSurface!=71 ||
      !p->Aquifers.Loaded || p->Aquifers.CellSizeX!=21 || !p->Ravines.Loaded || p->Ravines.Rarity!=17 || !p->Valleys.Loaded || p->Valleys.MaxDepth!=27 || !p->Lakes.Loaded)return false;
  const auto &image=p->BiomeMapImage;if(!image || image->Width!=64 || image->Height!=64 || image->ByteCount()!=12288)return false;
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto rgb=test::StbiFixtureRgb(x,y);for(int c=0;c<3;++c)if(image->Data()[(y*64+x)*3+c]!=rgb[c])return false;}return true;
}
inline bool OldPack(const std::shared_ptr<const WorldGenPack> &p)
{
  const unsigned char rgb[]={0x2d,0x8a,0x3f,0xe8,0xd4,0x6a,0x7a,0x7a,0x7a,0xd8,0xe8,0xf0};
  return p && p->Id=="old_composition" && p->BiomeMapImage && p->BiomeMapImage->Width==2 && p->BiomeMapImage->Height==2 &&
      p->BiomeMapImage->ByteCount()==sizeof(rgb) && std::equal(std::begin(rgb),std::end(rgb),p->BiomeMapImage->Data());
}
inline int Run(const char *path,bool owner_composition=false)
{
  const auto root=fs::path("build/desktop-msvc/s4-composition-fixtures");Pack(root/"old",true);Pack(root/"new",false);
  Write(root/"reset/pack.json",{{"id","composition_reset"}});const auto resetPath=(root/"reset").string(),oldPath=(root/"old").string(),newPath=(root/"new").string();
  (void)CaptureWorldGenContentSnapshot();std::cout<<size_t{0}<<std::endl;std::cerr<<size_t{0}<<std::endl;
  {UPlaceholderTextureCache warm(root/"warm_cache",16,glm::vec3(0.2f));(void)warm.GetOrCreateStem("warm",0,16);}
  Json report={{"schema","catalog-construction-composition.v1"},{"phases",Json::array()}};size_t failures=0;
  const auto packDomain=Inventory(root/"new",true);const auto packBound=BoundPackDirectorySource(packDomain);
  auto oldDecoder=std::make_shared<UStbiAllocationLedger>();std::array<WorldGenContentSnapshot,2> snapshots{};
  std::shared_ptr<UBlockMergeRegistry> merge;std::shared_ptr<UPlaceholderTextureCache> cache;std::shared_ptr<UBlockDefinitionStorage> definitions;std::shared_ptr<UTextureBaseStorage> bases;
  std::array<std::shared_ptr<const BlockDefinitionCatalog>,2> blockVersions{};std::string fingerprint;
  MeshBuildResult returnedMesh;RelightComputeResult returnedRelight;
  std::weak_ptr<const BlockDefinitionCatalog> returnedOnly;
  const auto resourcePacks=[&](bool old){auto p=merge_publication_fixture::Packs(root/(old?"resource_old":"resource_new"));p[0].Version=old?1:2;
    Write(p[0].Root/"blocks/graph_block.json",{{"name","graph_block"},{"id",kPackBlockIdMin+400},{"hardness",old?1.0f:78.0f},{"textures",Json::array({"common","common","common","common","common","common"})}});return p;};
  const auto beforePacks=resourcePacks(true),afterPacks=resourcePacks(false);
  {observation::Scope scope;UStbiAllocationScope active(oldDecoder);
    if(!UWorldGenPack::LoadFromDirectory(oldPath))return 1;snapshots[0]=CaptureWorldGenContentSnapshot();
    cache=std::make_shared<UPlaceholderTextureCache>(root/"cache",16,glm::vec3(0.2f),8);(void)cache->GetOrCreateStem("composition_pixel",0,16);
    merge=std::make_shared<UBlockMergeRegistry>();merge->Rebuild(beforePacks,cache,16);
    definitions=std::make_shared<UBlockDefinitionStorage>();merge->PopulateBlockDefinitionStorage(*definitions);blockVersions[0]=definitions->GetCatalogSnapshot();
    bases=std::make_shared<UTextureBaseStorage>();merge->PopulateTextureBaseStorage(*bases);
    if(owner_composition){
      auto extra=std::make_shared<UBlockDefinitionStorage>();BlockDefinition def;def.Name="returned_only";def.DisplayName=std::string(32768,'e');def.Hardness=31;
      extra->ReplaceAll({{8,def}},{{def.Name,8}});returnedMesh.InputCatalog=extra->GetCatalogSnapshot();returnedRelight.input_catalog=returnedMesh.InputCatalog;returnedOnly=returnedMesh.InputCatalog;
    }
  }
  snapshots[1]=snapshots[0];blockVersions[1]=blockVersions[0];
  const auto visit=[&](CatalogRuntimeDomain &out,bool returned){
    for(const auto &s:snapshots)out.Content(s);for(const auto &v:blockVersions)out.BlockCatalog(v);
    out.Merge(merge);out.Placeholder(cache);out.BlockStorage(definitions);out.TextureBases(bases);out.ActiveDirectory();out.CallerText(fingerprint);
    if(returned){out.MeshResult(returnedMesh);out.RelightResult(returnedRelight);}
  };
  const auto census=[&](){CatalogRuntimeDomain out;visit(out,owner_composition);return out.Catalogs();};
  auto sourceRoot=BoundRetainedCatalogRequests(census());const auto ownerCounts=BoundCatalogOwnerSource(OwnerCountProfile());
  const auto phase=[&](const char *name,size_t additional,bool supported,bool overflow,auto operation){
    const auto entryCensus=census();const auto entry=BoundRetainedCatalogRequests(entryCensus);const auto bound=ComposeCatalogConstruction(entry,additional,supported,overflow);
    detail::CandidateAdd(sourceRoot.requested_heap_upper,additional,sourceRoot.overflow);
    sourceRoot.supported=sourceRoot.supported && supported;sourceRoot.overflow=sourceRoot.overflow || overflow;
    const auto countBound=ComposeCatalogOwnerSource(ownerCounts,sourceRoot,entry,additional,supported,overflow);
    const auto entryObserved=observation::State.live+GetStbiProcessAllocationSnapshot().live_request_bytes;
    const auto retainedDecoder=GetStbiProcessAllocationSnapshot().live_request_bytes;
    auto decoder=std::make_shared<UStbiAllocationLedger>();bool completed=false;
    observation::State.peak=observation::State.live;observation::State.active=true;
    {UStbiAllocationScope active(decoder);completed=operation();}observation::State.active=false;
    const auto ordinaryPeak=observation::State.peak,newDecodePeak=decoder->GetSnapshot().peak_admitted_request_bytes;
    const auto componentEnvelope=ordinaryPeak+retainedDecoder+newDecodePeak;
    const bool ok=completed && bound.supported && !bound.overflow && entryCensus.decoder_request_bytes==retainedDecoder && entryObserved<=entry.requested_heap_upper && componentEnvelope<=bound.requested_heap_upper &&
      (!owner_composition || (countBound.supported && !countBound.overflow && componentEnvelope<=countBound.requested_heap_upper));
    if(!ok)++failures;report["phases"].push_back({{"name",name},{"pass",ok},{"entry_observed",entryObserved},{"entry_upper",entry.requested_heap_upper},{"retained_decoder",retainedDecoder},
        {"ordinary_peak",ordinaryPeak},{"new_decoder_peak",newDecodePeak},{"observed_component_envelope",componentEnvelope},{"composed_upper",bound.requested_heap_upper},{"owner_count_source_upper",countBound.requested_heap_upper}});
  };
  phase("whole_pack_directory",packBound.combined_additional_upper,packBound.supported,packBound.overflow,[&]{const bool result=UWorldGenPack::LoadFromDirectory(newPath);snapshots[1]=CaptureWorldGenContentSnapshot();return result;});
  const auto mergeDomain=ReadMergeSourceDomain(afterPacks,*merge,cache,16);const auto mergeBound=BoundMergeSource(mergeDomain);
  phase("merge_rebuild",mergeBound.additional_heap_upper,mergeBound.supported,mergeBound.overflow,[&]{merge->Rebuild(afterPacks,cache,16);return true;});
  const auto definitionBound=BoundDefinitionPublication(InspectDefinitionPublication(*merge));
  phase("definition_publication",definitionBound.requested_heap_upper,definitionBound.supported,definitionBound.overflow,[&]{merge->PopulateBlockDefinitionStorage(*definitions);blockVersions[1]=definitions->GetCatalogSnapshot();return true;});
  const auto textureBound=BoundTexturePublication(merge_publication_fixture::TextureDomain(afterPacks,cache));
  phase("texture_publication",textureBound.requested_heap_upper,textureBound.supported,textureBound.overflow,[&]{merge->PopulateTextureBaseStorage(*bases);return true;});
  const auto fingerprintBound=fingerprint_fixture::Bound(afterPacks,merge->GetNameToId());
  phase("fingerprint",fingerprintBound.requested_heap_upper,fingerprintBound.supported,fingerprintBound.overflow,[&]{fingerprint=merge->ComputeCatalogFingerprint();return true;});
  const bool values=OldPack(snapshots[0].Pack) && NewPack(snapshots[1].Pack) && snapshots[0].Pack->BiomeMapImage->Data()!=snapshots[1].Pack->BiomeMapImage->Data() &&
      blockVersions[0]->ById.at(kPackBlockIdMin+400).Hardness==1 && blockVersions[1]->ById.at(kPackBlockIdMin+400).Hardness==78 &&
      fingerprint==std::to_string(std::hash<std::string>{}(fingerprint_fixture::LegacyPayload(afterPacks,merge->GetNameToId())));
  if(!values)++failures;
  const auto retained=census();const auto retainedBound=BoundRetainedCatalogRequests(retained);
  const auto pairedRetained=observation::State.live+GetStbiProcessAllocationSnapshot().live_request_bytes;
  if(pairedRetained>retainedBound.requested_heap_upper || retained.decoder_request_bytes!=GetStbiProcessAllocationSnapshot().live_request_bytes)++failures;
  std::array<WorldGenContentSnapshot,64> aliases;for(auto &s:aliases)s=snapshots[0];
  const auto single=MeasureCatalogStorage(&snapshots[0],1),many=MeasureCatalogStorage(aliases.data(),aliases.size());
  const bool dedup=single.known_storage_bytes==many.known_storage_bytes && single.shared_owners==many.shared_owners && single.decoder_request_bytes==many.decoder_request_bytes;
  if(!dedup)++failures;for(auto &s:aliases)s={};
  report["retained"]={{"observed_components",pairedRetained},{"upper",retainedBound.requested_heap_upper},{"decoder_requests_once",retained.decoder_request_bytes},{"owners",retained.shared_owners},{"alias64_deduplicated",dedup}};
  if(owner_composition){
    CatalogRuntimeDomain joined,omitted,permuted;visit(joined,true);visit(omitted,false);
    for(int i=0;i<64;++i){permuted.MeshResult(returnedMesh);permuted.RelightResult(returnedRelight);visit(permuted,true);}
    const auto all=joined.Catalogs(),missing=omitted.Catalogs();
    const bool alias_dedup=all.known_storage_bytes==permuted.Catalogs().known_storage_bytes && all.shared_owners==permuted.Catalogs().shared_owners;
    const bool roots=all.Objects(CatalogObjectKind::Blocks)==3 && missing.Objects(CatalogObjectKind::Blocks)==2 &&
      all.known_storage_bytes>missing.known_storage_bytes && alias_dedup && returnedMesh.InputCatalog->ById.at(8).Hardness==31;
    auto controls=OwnerControls(sourceRoot,retainedBound);if(!roots || !controls["pass"].get<bool>())++failures;
    report["owner_composition"]={{"controls",controls},{"pass",roots},{"union_catalogs",all.Objects(CatalogObjectKind::Blocks)},
      {"omitted_returned_catalogs",missing.Objects(CatalogObjectKind::Blocks)},{"union_known_bytes",all.known_storage_bytes},
      {"omitted_known_bytes",missing.known_storage_bytes},{"alias64_cross_domain_deduplicated",alias_dedup},
      {"returned_types_scope","Actual mesh/relight result owner types populated directly with real catalog publication;real solver/mesh/queue semantics remain covered by retained RQ01/publication certificates"}};
  }
  report["old_current_oracles"]=values;
  if(!UWorldGenPack::LoadFromDirectory(resetPath))return 1;
  snapshots={};blockVersions={};merge.reset();cache.reset();definitions.reset();bases.reset();std::string{}.swap(fingerprint);
  returnedMesh={};returnedRelight={};if(owner_composition && !returnedOnly.expired())++failures;returnedOnly.reset();
  const bool graphRetired=!observation::State.live && !observation::State.blocks && !oldDecoder->GetSnapshot().live_request_bytes && !GetStbiProcessAllocationSnapshot().live_request_bytes;
  if(!graphRetired)++failures;report["graph_final_zero"]=graphRetired;
  if(!graphRetired){report["status"]="FAIL";report["remaining_ordinary"]=observation::State.live;report["remaining_decoder"]=GetStbiProcessAllocationSnapshot().live_request_bytes;
    if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}return 1;}
  // The entire real directory loader under every ordinary-new refusal. Its
  // existing optional-file catch/partial publication policy is retained.
  auto decoder=std::make_shared<UStbiAllocationLedger>();bool loaded=false;
  {observation::Scope scope;UStbiAllocationScope active(decoder);loaded=UWorldGenPack::LoadFromDirectory(newPath);}const auto normal=observation::State;
  bool normalOk=loaded && NewPack(UWorldGenPack::GetSnapshot()) && normal.peak<=packBound.ordinary_new_upper && decoder->GetSnapshot().peak_admitted_request_bytes<=packBound.new_decoder_peak_upper;
  UWorldGenPack::LoadFromDirectory(resetPath);normalOk=normalOk && !observation::State.live && !observation::State.blocks && !decoder->GetSnapshot().live_request_bytes;if(!normalOk)++failures;
  size_t prefixes=0,publishedAfterRefusal=0;
  if(normalOk)for(size_t i=0;i<normal.attempts;++i){decoder=std::make_shared<UStbiAllocationLedger>();bool threw=false,returned=false;
    {observation::Scope scope(i);UStbiAllocationScope active(decoder);try{returned=UWorldGenPack::LoadFromDirectory(newPath);}catch(const std::bad_alloc &){threw=true;}}
    const auto failed=observation::State;const bool published=UWorldGenPack::GetSnapshot()->Id=="new_composition";if(published)++publishedAfterRefusal;
    if(published && (threw || !returned))++failures;
    UWorldGenPack::LoadFromDirectory(resetPath);
    if(!failed.refusals || failed.peak>packBound.ordinary_new_upper || decoder->GetSnapshot().peak_admitted_request_bytes>packBound.new_decoder_peak_upper || observation::State.live || observation::State.blocks || decoder->GetSnapshot().live_request_bytes){++failures;break;}
    decoder=std::make_shared<UStbiAllocationLedger>();
    {observation::Scope scope;UStbiAllocationScope active(decoder);loaded=UWorldGenPack::LoadFromDirectory(newPath);}if(!loaded || !NewPack(UWorldGenPack::GetSnapshot()))++failures;
    UWorldGenPack::LoadFromDirectory(resetPath);if(observation::State.live || observation::State.blocks || decoder->GetSnapshot().live_request_bytes){++failures;break;}++prefixes;
  }
  report["directory"]={{"pass",normalOk},{"json_files",packDomain.json_files},{"biome_files",packDomain.biome_files},{"ordinary_attempts",normal.attempts},{"ordinary_peak",normal.peak},{"ordinary_upper",packBound.ordinary_new_upper},
      {"decoder_upper",packBound.new_decoder_peak_upper},{"refusal_and_exact_retry_cases",prefixes},{"legacy_publications_after_refusal",publishedAfterRefusal}};
  const auto huge=BoundPackDirectorySource({SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,0,{},false,true,false});
  auto censusHuge=retained;censusHuge.known_storage_bytes=SIZE_MAX;const auto retainedHuge=BoundRetainedCatalogRequests(censusHuge);
  const auto composedHuge=ComposeCatalogConstruction(retainedBound,SIZE_MAX);const bool overflow=huge.overflow && retainedHuge.overflow && composedHuge.overflow;
  if(!overflow)++failures;
  const auto phaseUpperForNegative=ComposeCatalogConstruction({},packBound.ordinary_new_upper).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(phaseUpperForNegative+1);::operator delete(extra);}const bool negative=observation::State.peak>phaseUpperForNegative && !observation::State.live;if(!negative)++failures;
  report["overflow_rejected"]=overflow;report["negative_control"]=negative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Real sequential pack+merge+definitions+textures+fingerprint graph. Prior source roots and new decoder scope separated;process decoder counters mirror census bytes,not extra ownership. Observed component envelope sums individual peaks conservatively,not synchronized RSS. Supplied default make_shared root profile only;all-world/thread/weak/unknown owner counts C06,CRT/OS/stack/GPU E12 and other loader/atlas/settings schedules still open.";
  if(owner_composition)report["scope"]="Real five-phase C05 pack/resource graph joined with escaped result catalog through one weak-only identity set;checked explicit source counts and supplied per-version history envelope. Catalog requested-data only;foreign thread/caller/root source domains must be supplied explicitly,other C05 schedules reused by unchanged construction composition mechanism. No process/CRT/stack/GPU/full matrix verdict.";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace catalog_composition_fixture
