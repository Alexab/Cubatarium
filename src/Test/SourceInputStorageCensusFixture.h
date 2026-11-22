#pragma once
#include "WorldGen/Core/SourceInputStorageCensus.h"
#include <optional>

static int RunSourceInputStorageCensusFixture(const char *reportPath)
{
  Json report={{"schema","source-input-retained-new-request-verification.v1"},{"status","PASS"},{"cases",Json::array()}};
  size_t failures=0;
  const auto check=[&](const char *name,const SourceInputStorageCensus &census,size_t expected) {
    const bool ok=!census.overflow && census.storage.unordered_model_supported && census.RequestedHeapComponents()==expected;
    if(!ok) ++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"observed_live_requests",expected},
      {"model_heap_components",census.RequestedHeapComponents()},{"known_bytes",census.storage.known_storage_bytes},
      {"unordered_metadata",census.storage.msvc_unordered_extra_bytes},{"tree_metadata",census.storage.msvc_tree_extra_bytes},
      {"large_alignment_extra",census.msvc_large_allocation_extra_bytes},{"json_values",census.json_values},
      {"settings_roots",census.settings_roots},{"settings_inline_bytes",census.settings_inline_bytes}});
  };
  const auto cleanup=[&] {if(observation::State.live || observation::State.blocks) ++failures;};
  std::optional<WorldGenSets> settings,previous;
  {observation::Scope scope;settings.emplace();}
  const WorldGenSets *one[]={&*settings};
  check("settings_empty6_maps",MeasureSourceInputStorage(one,1),observation::State.live);
  // Keep the same epoch as allocations grow; the observer is enabled only
  // during actual mutations and disabled during independent census/reporting.
  observation::State.active=true;
  auto &section=settings->VegetationSections[std::string(4097,'k')];
  settings->VegetationSections.reserve(2048);section.Entries.reserve(37);
  WorldGenObjectEntry entry;entry.ObjectName.reserve(8193);entry.ObjectName="x";
  entry.Biomes.reserve(129);entry.Biomes.push_back(std::string(5001,'b'));
  entry.SubBiomes.reserve(133);entry.SubBiomes.push_back("woodland");
  section.Entries.push_back(std::move(entry));
  settings->Terrain["surface"].Block=std::string(4097,'s');settings->Ores["copper"]={false,1.25f};
  observation::State.active=false;
  check("settings_spare_capacities_and_bucket_growth",MeasureSourceInputStorage(one,1),observation::State.live);
  observation::State.active=true;previous.emplace(*settings);observation::State.active=false;
  const WorldGenSets *versions[]={&*settings,&*previous,&*settings};
  const auto combined=MeasureSourceInputStorage(versions,3);
  check("settings_prior_current_duplicate_reference",combined,observation::State.live);
  if(combined.settings_roots!=2) ++failures;
  observation::State.active=true;settings->VegetationSections.begin()->second.Entries.clear();observation::State.active=false;
  check("settings_clear_keeps_vector_capacity",MeasureSourceInputStorage(versions,3),observation::State.live);
  settings.reset();previous.reset();cleanup();

  std::optional<Json> document;
  {observation::Scope scope;document.emplace(Json::object());}
  const Json *root[]={&*document};check("json_empty_object",MeasureSourceInputStorage(nullptr,0,root,1),observation::State.live);
  observation::State.active=true;
  (*document)["array"]=Json::array();auto &array=(*document)["array"].get_ref<Json::array_t&>();array.reserve(513);
  array.push_back(std::string(5001,'t'));array.push_back(Json::object());array.back()["text"]=std::string(8193,'u');
  std::vector<uint8_t> bytes;bytes.reserve(9001);bytes.resize(7);(*document)["binary"]=Json::binary(std::move(bytes),19);
  (*document)[std::string(4097,'k')]=false;
  for(int i=0;i<257;++i) (*document)["field"+std::to_string(i)]=i;
  observation::State.active=false;
  check("json_string_binary_array_spare_tree_growth",MeasureSourceInputStorage(nullptr,0,root,1),observation::State.live);
  const Json *aliases[]={&(*document)["array"][1],&*document,&(*document)["array"][0],&*document};
  check("json_child_first_and_repeated_root",MeasureSourceInputStorage(nullptr,0,aliases,4),observation::State.live);
  observation::State.active=true;(*document)["array"].clear();observation::State.active=false;
  check("json_clear_keeps_array_capacity",MeasureSourceInputStorage(nullptr,0,root,1),observation::State.live);
  document.reset();cleanup();
  {observation::Scope scope;
    document.emplace(0);for(int i=0;i<4096;++i) {Json parent=Json::array();parent.push_back(std::move(*document));*document=std::move(parent);}
  }
  const auto deep=MeasureSourceInputStorage(nullptr,0,root,1);
  check("json_deep4096_iterative_census",deep,observation::State.live);
  if(deep.json_values!=4097) ++failures;
  document.reset();cleanup();
  {observation::Scope scope;settings.emplace();document.emplace(Json::array({"shared input",1,2}));}
  const WorldGenSets *joinedSettings[]={&*settings};const Json *joinedJson[]={&*document};
  const auto joined=MeasureSourceInputStorage(joinedSettings,1,joinedJson,1);
  check("settings_and_external_dom_join",joined,observation::State.live);
  // An unmodeled extra allocation must change the observed/model difference.
  observation::State.active=true;void *extra=::operator new(97);observation::State.active=false;
  const bool negative=MeasureSourceInputStorage(joinedSettings,1,joinedJson,1).RequestedHeapComponents()!=observation::State.live;
  ::operator delete(extra);report["unmodeled_allocation_negative_control"]=negative?"PASS":"FAIL";if(!negative) ++failures;
  settings.reset();document.reset();cleanup();
  report["status"]=failures?"FAIL":"PASS";report["failed_cases"]=failures;
  report["scope"]="Supplied WorldGenSets and default nlohmann::json retained requested-new components,including current MSVC alignment shim;inline roots/reference arrays/collector scratch/CRT/shared-control/process roots excluded";
  if(reportPath) {std::ofstream file(reportPath);file<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
}
