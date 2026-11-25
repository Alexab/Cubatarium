#pragma once
#include "Test/OrdinaryNewObservation.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/CatalogFingerprint.h"
#include "WorldGen/Core/FingerprintSourceEnvelope.h"
#include <cstdio>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fingerprint_fixture
{
inline std::FILE *TerminationReport{};
inline size_t Prefix=SIZE_MAX;
inline void ReportTermination() noexcept
{
  const auto &s=observation::State;
  if(TerminationReport){std::fprintf(TerminationReport,"{\"status\":\"TERMINATION_OBSERVED\",\"prefix\":%zu,\"attempts\":%zu,\"refusals\":%zu,\"live\":%zu,\"blocks\":%zu}\n",Prefix,s.attempts,s.refusals,s.live,s.blocks);std::fflush(TerminationReport);}
  std::_Exit(86);
}
inline std::vector<cutum::ResourcePackManifest> Packs()
{
  std::vector<cutum::ResourcePackManifest> packs(3);
  for(size_t i=0;i<packs.size();++i){packs[i].Id="fingerprint_pack_"+std::to_string(i);packs[i].Version=int(i)-1;packs[i].Root="build/desktop-msvc/s4-fingerprint-empty-pack";}
  std::filesystem::create_directories(packs.front().Root);return packs;
}
inline int Baseline(const char *path,size_t prefix)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto packs=Packs();UBlockMergeRegistry merge;merge.Rebuild(packs,nullptr,16);
  for(const auto *name:{"zeta","alpha","middle"})merge.ResolveName(name);
  const auto expected=merge.ComputeCatalogFingerprint();std::string actual;bool refused=false;
  Prefix=prefix;TerminationReport=std::fopen(path,"wb");if(!TerminationReport)return 1;
  const auto previous=std::set_terminate(ReportTermination);
  {observation::Scope scope(prefix);try{actual=merge.ComputeCatalogFingerprint();}catch(const std::bad_alloc &){refused=true;}}
  const auto counts=observation::State;const bool same=actual==expected;std::string{}.swap(actual);
  std::set_terminate(previous);std::fclose(TerminationReport);TerminationReport=nullptr;
  const bool ok=!observation::State.live && !observation::State.blocks && (prefix==SIZE_MAX?same:refused);
  Json report={{"status",ok?"PASS":"FAIL"},{"prefix",prefix},{"attempts",counts.attempts},{"peak",counts.peak},{"retained_result",counts.live},
      {"final_live",observation::State.live},{"final_blocks",observation::State.blocks},{"refused",refused},{"fingerprint",expected}};
  {std::ofstream out(path);out<<report.dump(2)<<'\n';}return ok?0:1;
}

inline std::string LegacyPayload(const std::vector<cutum::ResourcePackManifest> &packs,
    const std::unordered_map<std::string,cutum::BlockId> &names)
{
  using Json=nlohmann::json;Json root,packRows=Json::array(),blockRows=Json::array();
  for(const auto &p:packs)packRows.push_back({{"id",p.Id},{"version",p.Version}});root["packs"]=packRows;
  std::vector<std::pair<std::string,cutum::BlockId>> sorted(names.begin(),names.end());std::sort(sorted.begin(),sorted.end());
  for(const auto &entry:sorted)blockRows.push_back({{"name",entry.first},{"id",entry.second}});root["blocks"]=blockRows;return root.dump();
}
inline cutum::FingerprintSourceEnvelope Bound(const std::vector<cutum::ResourcePackManifest> &packs,
    const std::unordered_map<std::string,cutum::BlockId> &names)
{
  size_t bytes=0,largest=0;for(const auto &p:packs){bytes+=p.Id.size();largest=std::max(largest,p.Id.size());}
  for(const auto &p:names){bytes+=p.first.size();largest=std::max(largest,p.first.size());}
  return cutum::BoundFingerprintSource(packs.size(),names.size(),bytes,largest);
}
inline size_t StringRequest(const std::string &text)
{
  const auto data=reinterpret_cast<uintptr_t>(text.data()),object=reinterpret_cast<uintptr_t>(&text);
  if(data>=object && data-object<sizeof(text))return 0;
  auto bytes=text.capacity()+1;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  if(bytes>=std::_Big_allocation_threshold)bytes+=std::_Non_user_size;
#endif
  return bytes;
}
inline int Accepted(const char *path)
{
  using namespace cutum;using Json=nlohmann::json;using Names=std::unordered_map<std::string,BlockId>;
  Json report={{"schema","fingerprint-source.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  const auto run=[&](const char *name,const auto &packs,const Names &names){
    const auto expected=LegacyPayload(packs,names);const auto bound=Bound(packs,names);std::string actual;
    {observation::Scope scope;actual=BuildCatalogFingerprintPayload(packs,names);}
    const auto normal=observation::State;const bool semantic=actual==expected;std::string{}.swap(actual);
    const bool ok=semantic && bound.supported && !bound.overflow && expected.size()<=bound.payload_bytes_upper && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",name},{"pass",ok},{"attempts",normal.attempts},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"payload_bytes",expected.size()}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){bool refused=false;Prefix=i;
      {observation::Scope scope(i);try{actual=BuildCatalogFingerprintPayload(packs,names);}catch(const std::bad_alloc &){refused=true;}}
      std::string{}.swap(actual);if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      {observation::Scope scope;actual=BuildCatalogFingerprintPayload(packs,names);}
      if(actual!=expected)++failures;std::string{}.swap(actual);if(observation::State.live || observation::State.blocks)++failures;++prefixes;
    }
    if(LegacyPayload(packs,names)!=expected)++failures;
  };
  std::vector<ResourcePackManifest> none;Names empty;run("empty",none,empty);
  const auto packs=Packs();UBlockMergeRegistry merge;merge.Rebuild(packs,nullptr,16);
  for(const auto *name:{"zeta","alpha","middle"})merge.ResolveName(name);
  run("real_registry_inputs",packs,merge.GetNameToId());
  const auto oldPayload=LegacyPayload(packs,merge.GetNameToId());const auto expectedHash=std::to_string(std::hash<std::string>{}(oldPayload));
  std::string fingerprint;const auto realBound=Bound(packs,merge.GetNameToId());
  {observation::Scope scope;fingerprint=merge.ComputeCatalogFingerprint();}const auto real=observation::State;
  const bool realOk=fingerprint==expectedHash && fingerprint=="15877548034598418822" && real.peak<=realBound.requested_heap_upper;
  std::string{}.swap(fingerprint);if(!realOk || observation::State.live || observation::State.blocks)++failures;
  for(size_t i=0;i<real.attempts;++i){bool refused=false;Prefix=i;
    {observation::Scope scope(i);try{fingerprint=merge.ComputeCatalogFingerprint();}catch(const std::bad_alloc &){refused=true;}}
    std::string{}.swap(fingerprint);if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>realBound.requested_heap_upper)++failures;
    {observation::Scope scope;fingerprint=merge.ComputeCatalogFingerprint();}if(fingerprint!=expectedHash)++failures;std::string{}.swap(fingerprint);
    if(observation::State.live || observation::State.blocks)++failures;++prefixes;
  }
  const bool realUnchanged=LegacyPayload(packs,merge.GetNameToId())==oldPayload;if(!realUnchanged)++failures;
  report["real_registry"]={{"pass",realOk},{"fingerprint",expectedHash},{"peak",real.peak},{"attempts",real.attempts},{"source_unchanged",realUnchanged}};
  std::vector<ResourcePackManifest> unusual(4);unusual[0].Id="quote\"slash\\/";unusual[0].Version=std::numeric_limits<int>::min();
  unusual[1].Id="\xD0\x91\xD0\xB8\xD0\xBE\xD0\xBC \xF0\x9F\x8C\xB2";unusual[1].Version=std::numeric_limits<int>::max();
  unusual[2].Id=std::string("null\0pack",9);unusual[3].Id="";
  std::string controls;for(int i=0;i<32;++i)controls.push_back(char(i));
  Names escaped{{controls,std::numeric_limits<BlockId>::max()},{"",0},{"\xF0\x9F\x8C\xB2",7},{"quote\"slash\\/",3}};
  run("escaping_utf8_nul_integer_edges",unusual,escaped);
  auto reversed=unusual;std::reverse(reversed.begin(),reversed.end());
  const bool packOrder=BuildCatalogFingerprintPayload(reversed,escaped)==LegacyPayload(reversed,escaped) && BuildCatalogFingerprintPayload(reversed,escaped)!=LegacyPayload(unusual,escaped);
  Names reordered;for(const auto &row:escaped)reordered.emplace(row);const bool nameOrder=BuildCatalogFingerprintPayload(unusual,reordered)==LegacyPayload(unusual,escaped);
  if(!packOrder || !nameOrder)++failures;
  Names wide;for(int i=0;i<257;++i)wide.emplace("wide_"+std::to_string(i),static_cast<BlockId>(i));run("257_names",packs,wide);
  auto longPacks=packs;longPacks[0].Id=std::string(8193,'p');Names longNames{{std::string(131073,'n'),9},{std::string(4097,'\t'),10}};run("long_fields",longPacks,longNames);
  // Strict UTF8 errors use the same library scalar serializer and exact text.
  for(bool invalidPack:{false,true}){
    auto p=packs;Names n{{"valid",1}};if(invalidPack)p[0].Id="\xF0\x9F";else n.emplace("\xC0\xAF",2);
    std::string expectedError,actualError;int expectedId=0,actualId=0;
    try{(void)LegacyPayload(p,n);}catch(const Json::exception &e){expectedError=e.what();expectedId=e.id;}
    std::exception_ptr error;
    {observation::Scope scope;try{(void)BuildCatalogFingerprintPayload(p,n);}catch(...){error=std::current_exception();}}
    const auto normal=observation::State;
    try{std::rethrow_exception(error);}catch(const Json::exception &e){actualError=e.what();actualId=e.id;}
    error=nullptr;const auto bound=Bound(p,n);bool ok=expectedId==316 && actualId==expectedId && actualError==expectedError &&
        normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;if(!ok)++failures;
    report["cases"].push_back({{"name",invalidPack?"invalid_utf8_pack":"invalid_utf8_name"},{"pass",ok},{"attempts",normal.attempts},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"error_id",actualId}});
    for(size_t i=0;i<normal.attempts;++i){bool refused=false;Prefix=i;
      {observation::Scope scope(i);try{(void)BuildCatalogFingerprintPayload(p,n);}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      std::exception_ptr retryError;
      {observation::Scope scope;try{(void)BuildCatalogFingerprintPayload(p,n);}catch(...){retryError=std::current_exception();}}
      try{std::rethrow_exception(retryError);}catch(const Json::exception &e){if(e.id!=expectedId || std::string(e.what())!=expectedError)++failures;}
      retryError=nullptr;if(observation::State.live || observation::State.blocks)++failures;++prefixes;
    }
  }
  // Returned payload owns its bytes independently of borrowed input versions.
  std::string prior,current;size_t entry=0,entryObserved=0;
  {observation::Scope scope;prior=BuildCatalogFingerprintPayload(longPacks,longNames);entry=StringRequest(prior);entryObserved=observation::State.live;current=BuildCatalogFingerprintPayload(unusual,escaped);}
  const auto overlap=observation::State;const auto oldBound=Bound(longPacks,longNames),nextBound=Bound(unusual,escaped);
  const auto overlapUpper=std::max(oldBound.requested_heap_upper,entry+nextBound.requested_heap_upper);
  const bool priorOk=entry==entryObserved && prior==LegacyPayload(longPacks,longNames) && current==LegacyPayload(unusual,escaped) && overlap.peak<=overlapUpper;
  std::string{}.swap(prior);std::string{}.swap(current);if(!priorOk || observation::State.live || observation::State.blocks)++failures;
  report["prior_current"]={{"pass",priorOk},{"entry_request",entry},{"entry_observed",entryObserved},{"peak",overlap.peak},{"upper",overlapUpper}};
  std::string owned,ownedExpected;
  {auto inputPacks=packs;Names inputNames{{std::string(8193,'x'),17}};ownedExpected=LegacyPayload(inputPacks,inputNames);
    {observation::Scope scope;owned=BuildCatalogFingerprintPayload(inputPacks,inputNames);}}
  const bool ownsText=owned==ownedExpected;std::string{}.swap(owned);if(!ownsText || observation::State.live || observation::State.blocks)++failures;
  report["returned_payload_outlives_inputs"]=ownsText;
  const auto overflow=BoundFingerprintSource(SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX);if(!overflow.overflow)++failures;
  const auto upper=BoundFingerprintSource(0,0,0,0).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["pack_order_preserved"]=packOrder;report["map_insertion_order_irrelevant"]=nameOrder;report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;
  report["persistent_refusal_cases"]=prefixes;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Exact legacy canonical payload/hash and strict UTF8 errors;source ordinary-new model through cleanup/returned result. Borrowed catalog,prior returned buffers,CRT/locale/exception runtime/stack/process memory separate. Not full C05 publication acceptance.";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace fingerprint_fixture
