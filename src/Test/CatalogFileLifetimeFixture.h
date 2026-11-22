#pragma once
#include "WorldGen/Core/CatalogJsonSource.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <optional>

static bool CanOpenCatalogFileExclusively(const std::filesystem::path &path)
{
#if defined(_WIN32)
  const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);return true;
#else
  (void)path;return false;
#endif
}
static int RunCatalogFileLifetimeFixture(const char *reportPath)
{
  Json report={{"schema","catalog-file-source-lifetime.v1"},{"status","PASS"},{"baseline",Json::array()},{"fixed",Json::array()},{"paths",Json::array()}};
#if !defined(_WIN32)
  report["status"]="UNTESTED";report["reason"]="Windows exclusive file handle oracle required";
  if(reportPath) {std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}return 2;
#else
  size_t failures=0,persistent=0;
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  std::fprintf(stderr,"phase: create input\n");
  const auto folder=std::filesystem::temp_directory_path()/(L"cubatarium_catalog_file_"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(folder);
  const auto filePath=folder/L"source_данные.json";
  const auto write=[&](const std::string &text){std::ofstream file(filePath,std::ios::binary);file.write(text.data(),text.size());};
  // Exact previous production statement semantics,using the intentionally
  // caller-owned stream API as control. This API itself is not defective.
  for(bool malformed:{false,true}) {
    std::fprintf(stderr,"phase: old statement malformed=%d\n",int(malformed));
    write(malformed?"{":"{\"name\":\"old-scope\"}");
    std::ifstream file(filePath);bool threw=false;
    try {const auto root=ParseCatalogSourceJson(file,CatalogJsonKind::Pack);}catch(const Json::exception &){threw=true;}
    const bool exclusive=CanOpenCatalogFileExclusively(filePath);
    const bool retained=file.is_open() && !exclusive;
    report["baseline"].push_back({{"malformed",malformed},{"exception",threw},{"file_still_open",file.is_open()},{"exclusive_open",exclusive}});
    if(!retained || threw!=malformed) ++failures;
  }
  const std::vector<std::pair<std::string,std::string>> inputs={
    {"valid",R"({"name":"valid","rows":[1,2,3]})"},
    {"BOM_CRLF",std::string("\xEF\xBB\xBF")+"{\r\n\"name\":\"unicode-path\"\r\n}"},
    {"long",std::string("{\"name\":\"")+std::string(8193,'n')+"\",\"rows\":[1,2,3]}"},
    {"duplicate_subtrees",R"({"x":[{}, {"keep":[1,2,3]}],"x":{"leaf":true},"x":[4]})"},
    {"deep",std::string(1024,'[')+"0"+std::string(1024,']')},
    {"malformed","{"},{"trailing","{} 0"}};
  for(const auto &[name,text]:inputs) {
    std::fprintf(stderr,"phase: fixed %s\n",name.c_str());
    write(text);Json expected;std::string expectedError;
    {std::ifstream reference(filePath);try{expected=Json::parse(reference);}catch(const Json::exception &e){expectedError=e.what();}}
    std::ifstream file(filePath);const bool previouslyLocked=!CanOpenCatalogFileExclusively(filePath);
    CatalogJsonDocument actual;std::string error;
    {observation::Scope scope;try{actual=ParseAndCloseCatalogFileJson(file,CatalogJsonKind::Pack);}catch(const Json::exception &e){error=e.what();}}
    auto counts=observation::State;const auto shape=GetLastCatalogJsonSource();
    const bool closed=!file.is_open() && CanOpenCatalogFileExclusively(filePath);
    const bool equivalent=expectedError.empty()?error.empty()&&actual==expected:error==expectedError;
    std::string().swap(error);counts.live=observation::State.live;
    const Json *roots[]={&actual};const auto storage=MeasureSourceInputStorage(nullptr,0,roots,1);
    const bool retainedExact=storage.RequestedHeapComponents()+actual.RetirementRequestedBytes()==counts.live;
    actual=CatalogJsonDocument{};std::string().swap(error);counts.live=observation::State.live;counts.blocks=observation::State.blocks;
    const bool ok=previouslyLocked && closed && equivalent && retainedExact && counts.live==0 && counts.blocks==0 &&
      shape.model_supported && !shape.overflow && counts.peak<=shape.requested_heap_upper;
    if(!ok) ++failures;
    report["fixed"].push_back({{"name",name},{"pass",ok},{"closed_before_outer_stream_destruction",closed},
      {"source_upper",shape.requested_heap_upper},{"peak",counts.peak},{"attempts",counts.attempts},{"final_live",counts.live},
      {"retained_request_components_exact",retainedExact}});
    if((name=="long" || name=="duplicate_subtrees" || name=="deep") && ok) for(size_t index=0;index<counts.attempts;++index) {
      std::fprintf(stderr,"phase: refusal prefix %zu\n",index);
      std::ifstream denied(filePath);bool refused=false;
      {observation::Scope scope(index);try{actual=ParseAndCloseCatalogFileJson(denied,CatalogJsonKind::Pack);}catch(const std::bad_alloc &){refused=true;}}
      const bool released=!denied.is_open() && CanOpenCatalogFileExclusively(filePath);
      actual=CatalogJsonDocument{};
      if(!refused || !released || observation::State.live || observation::State.blocks) ++failures;
      std::ifstream retry(filePath);
      {observation::Scope scope;actual=ParseAndCloseCatalogFileJson(retry,CatalogJsonKind::Pack);}
      if(actual!=expected || retry.is_open() || !CanOpenCatalogFileExclusively(filePath)) ++failures;
      actual=CatalogJsonDocument{};if(observation::State.live || observation::State.blocks) ++failures;++persistent;
    }
    if(ok && expectedError.empty()) {
      std::ifstream candidate(filePath);size_t beforeRetire=0;bool refused=false;
      {observation::Scope scope;
        try {auto document=ParseAndCloseCatalogFileJson(candidate,CatalogJsonKind::Pack);
          beforeRetire=observation::State.attempts;observation::State.fail_after=beforeRetire;
          void *extra=::operator new(1);::operator delete(extra);
        }catch(const std::bad_alloc &){refused=true;}
      }
      const bool retired=refused && observation::State.attempts==beforeRetire+1 && observation::State.live==0 &&
        observation::State.blocks==0 && !candidate.is_open() && CanOpenCatalogFileExclusively(filePath);
      if(!retired) ++failures;
      report["candidate_failure_retirement"].push_back({{"name",name},{"pass",retired},{"new_requests_during_retirement",observation::State.attempts-beforeRetire-1}});
    }
  }
  // Parser error on an unopened stream keeps the same exception and owns no
  // file handle. Missing input is still rejected by existing caller checks.
  {
    std::fprintf(stderr,"phase: unopened\n");
    std::ifstream unopened;bool threw=false;try{(void)ParseAndCloseCatalogFileJson(unopened,CatalogJsonKind::Pack);}catch(const Json::exception &){threw=true;}
    if(!threw || unopened.is_open()) ++failures;
    report["unopened_stream_rejected"]=threw&&!unopened.is_open();
  }
  std::optional<std::filesystem::path> path,derived;
  std::fprintf(stderr,"phase: path capacity\n");
  {observation::Scope scope;std::wstring native;native.reserve(8193);native=L"short";path.emplace(std::move(native));derived.emplace(*path/L"child.json");}
  const std::filesystem::path *paths[]={&*path,&*derived,&*path};
  auto census=MeasureSourceInputStorage(nullptr,0,nullptr,0,paths,3);
  const bool exact=!census.overflow && census.path_roots==2 && census.RequestedHeapComponents()==observation::State.live;
  report["paths"].push_back({{"name","native_capacity_join_and_duplicate"},{"pass",exact},{"observed_live",observation::State.live},
    {"heap_components",census.RequestedHeapComponents()},{"inline_bytes",census.path_inline_bytes},{"large_alignment_extra",census.msvc_large_allocation_extra_bytes}});
  if(!exact) ++failures;path.reset();derived.reset();if(observation::State.live || observation::State.blocks) ++failures;
  DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
  std::fprintf(stderr,"phase: handle cohorts\n");
  write("{\"name\":\"repeat\"}");
  for(int i=0;i<128;++i) {std::ifstream file(filePath);const auto root=ParseAndCloseCatalogFileJson(file,CatalogJsonKind::Pack);if(file.is_open()) ++failures;}
  GetProcessHandleCount(GetCurrentProcess(),&after);
  report["handle_count_before"]=before;report["handle_count_after"]=after;report["handle_cohorts"]=128;
  if(before!=after) ++failures;
  std::filesystem::remove(filePath);std::filesystem::remove(folder);
  report["persistent_refusal_and_retry_cases"]=persistent;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="19 production catalog file statements use close-before-candidate helper;valid/error/ordinary-new refusal file handle release and actual native path capacity. Stream public object sizes are inline;direct CRT buffer/locale/filesystem runtime/OS search handle metadata and all caller source paths remain separate E12/source-domain obligations.";
  if(reportPath) {std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
#endif
}
