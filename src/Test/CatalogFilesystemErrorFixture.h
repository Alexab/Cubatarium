#pragma once
#include "WorldGen/Core/CatalogFilesystemSourceEnvelope.h"
#include <functional>

static int RunCatalogFilesystemErrorFixture(const char *reportPath)
{
  namespace fs=std::filesystem;
  Json report={{"schema","catalog-filesystem-error-source-requests.v1"},{"status","PASS"},{"cases",Json::array()}};
#if !defined(_WIN32)
  report["status"]="UNTESTED";if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}return 2;
#else
  size_t failures=0,persistent=0;
  const auto parent=fs::temp_directory_path()/(L"cuba_fserr_"+std::to_wstring(GetCurrentProcessId()));fs::create_directory(parent);
  const auto missing=parent/L"нет_каталога",target=parent/L"missing_target",regular=parent/L"regular.txt";
  {std::ofstream out(regular);out<<"fixture";}
  const auto handles=[] {DWORD count=0;GetProcessHandleCount(GetCurrentProcess(),&count);return count;};
  struct Scene {
    std::string name,operation;std::error_code code;fs::path first,second;
    std::function<void()> invoke;bool exactText{};
  };
  std::vector<Scene> scenes;
  const std::error_code missingCode(ERROR_PATH_NOT_FOUND,std::system_category());
  const auto addConstructor=[&](const char *name,const std::string &operation,const fs::path &first,const fs::path &second,const std::error_code &code,int paths) {
    scenes.push_back({name,operation,code,first,second,[=] {
      if(paths==0) throw fs::filesystem_error(operation,code);
      if(paths==1) throw fs::filesystem_error(operation,first,code);
      throw fs::filesystem_error(operation,first,second,code);
    },true});
  };
  addConstructor("no_path","directory_iterator::operator++",{},{},missingCode,0);
  addConstructor("one_unicode_path","directory_iterator::directory_iterator",missing,{},missingCode,1);
  addConstructor("two_paths","rename",missing,target,missingCode,2);
  addConstructor("long_operation_and_paths",std::string(4097,'o'),fs::path(std::wstring(8193,L'p')),fs::path(std::wstring(5001,L'q')),missingCode,2);
  addConstructor("unknown_system_code","file_size",missing,{},std::error_code(0x7fffffff,std::system_category()),1);
  addConstructor("generic_category","path conversion",missing,{},std::make_error_code(std::errc::invalid_argument),1);
  // Native path copies preserve malformed UTF16 too;pretty-message conversion
  // uses the SDK replacement-character path,not the strict conversion API.
  addConstructor("replacement_for_unpaired_native_unit","format",fs::path(std::wstring(1,wchar_t{0xd800})),{},missingCode,1);
  scenes.back().exactText=false;
  const auto addActual=[&](const char *name,const char *operation,const fs::path &first,const fs::path &second,std::function<void()> action) {
    std::error_code code;
    try{action();}catch(const fs::filesystem_error &e){code=e.code();}
    if(!code) {++failures;return;}
    scenes.push_back({name,operation,code,first,second,std::move(action),true});
  };
  addActual("actual_missing_directory","directory_iterator::directory_iterator",missing,{},[=]{fs::directory_iterator it(missing);});
  addActual("actual_recursive_missing_directory","recursive_directory_iterator::recursive_directory_iterator",missing,{},[=]{fs::recursive_directory_iterator it(missing);});
  addActual("actual_not_a_directory","directory_iterator::directory_iterator",regular,{},[=]{fs::directory_iterator it(regular);});
  addActual("actual_file_size_missing","file_size",missing,{},[=]{(void)fs::file_size(missing);});
  addActual("actual_rename_missing","rename",missing,target,[=]{fs::rename(missing,target);});
  for(const auto &scene:scenes) {
    // Obtain actual localized source domain before observation;warm category
    // initialization. Do not infer a universal OS buffer allocation size.
    const std::string categoryMessage=scene.code.message();
    const auto bound=BoundCatalogFilesystemErrorSource({scene.operation.size(),categoryMessage.size(),scene.first.native().size(),scene.second.native().size(),4});
    std::string expected=scene.operation+": "+categoryMessage;
    if(!scene.first.empty() || !scene.second.empty()) {
      if(scene.exactText) expected+=": \""+scene.first.string()+(scene.second.empty()?"\"":"\", \""+scene.second.string()+"\"");
    }
    bool equivalent=false;const DWORD before=handles();
    {
      observation::Scope scope;
      try {scene.invoke();}catch(const fs::filesystem_error &e) {
        equivalent=e.code()==scene.code && e.path1()==scene.first && e.path2()==scene.second &&
          (scene.exactText?std::string_view(e.what())==expected:std::string_view(e.what()).find(scene.operation)==0);
      }
    }
    const auto counts=observation::State;
    const bool ok=equivalent && bound.supported && !bound.overflow && counts.peak<=bound.new_request_upper &&
      counts.live==0 && counts.blocks==0 && handles()==before;
    if(!ok) ++failures;
    report["cases"].push_back({{"name",scene.name},{"pass",ok},{"message_bytes",categoryMessage.size()},
      {"operation_bytes",scene.operation.size()},{"path1_units",scene.first.native().size()},{"path2_units",scene.second.native().size()},
      {"source_upper",bound.new_request_upper},{"peak",counts.peak},{"attempts",counts.attempts},{"final_live",counts.live},
      {"handle_count_restored",handles()==before}});
    if(ok) for(size_t i=0;i<counts.attempts;++i) {
      bool refused=false;
      {observation::Scope scope(i);try{scene.invoke();}catch(const std::bad_alloc &){refused=true;}catch(const fs::filesystem_error &){}}
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.new_request_upper || handles()!=before) ++failures;
      {observation::Scope scope;try{scene.invoke();}catch(const fs::filesystem_error &e){if(e.code()!=scene.code) ++failures;}}
      if(observation::State.live || observation::State.blocks || handles()!=before) ++failures;++persistent;
    }
  }
  // Guaranteed strict Unicode conversion rejection from the pinned UTF32
  // range check;no dependence on whether ANSI best-fit accepts a character.
  const auto invalidCode=std::make_error_code(std::errc::invalid_argument);
  const auto invalidMessage=invalidCode.message();const auto conversionBound=BoundCatalogFilesystemErrorSource({0,invalidMessage.size(),0,0,4});
  bool rejected=false;
  {observation::Scope scope;try{fs::path invalid(std::u32string(1,char32_t{0x110000}));}catch(const std::system_error &e){rejected=e.code()==invalidCode;}}
  const auto conversionCounts=observation::State;
  const bool conversionOk=rejected && observation::State.live==0 && observation::State.blocks==0 && observation::State.peak<=conversionBound.new_request_upper;
  report["strict_utf32_invalid_rejected"]=conversionOk;if(!conversionOk) ++failures;
  for(size_t i=0;i<conversionCounts.attempts;++i) {
    bool refused=false;{observation::Scope scope(i);try{fs::path invalid(std::u32string(1,char32_t{0x110000}));}catch(const std::bad_alloc &){refused=true;}}
    if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>conversionBound.new_request_upper) ++failures;
    {observation::Scope scope;try{fs::path invalid(std::u32string(1,char32_t{0x110000}));}catch(const std::system_error &e){if(e.code()!=invalidCode) ++failures;}}
    if(observation::State.live || observation::State.blocks) ++failures;++persistent;
  }
  // Copy keeps error text/path values but is a distinct ordinary-new owner.
  fs::filesystem_error original(std::string(4097,'c'),fs::path(std::wstring(8193,L'a')),missingCode);
  std::optional<fs::filesystem_error> copy;const auto copyBound=BoundCatalogFilesystemErrorSource({4097,missingCode.message().size(),8193,0,4});
  {observation::Scope scope;copy.emplace(original);}
  const auto copyCounts=observation::State;
  const bool same=std::string_view(copy->what())==original.what() && copy->path1()==original.path1() &&
    copy->what()!=original.what() && copy->path1().native().data()!=original.path1().native().data() && copyCounts.live>0;
  copy.reset();
  const bool copyOk=same && observation::State.live==0 && observation::State.blocks==0 && observation::State.peak<=copyBound.new_request_upper;
  report["copied_error_owns_independent_paths_and_text"]=copyOk;if(!copyOk) ++failures;
  for(size_t i=0;i<copyCounts.attempts;++i) {
    bool refused=false;{observation::Scope scope(i);try{copy.emplace(original);}catch(const std::bad_alloc &){refused=true;}}
    copy.reset();if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>copyBound.new_request_upper) ++failures;
    {observation::Scope scope;copy.emplace(original);}
    if(std::string_view(copy->what())!=original.what() || copy->path1()!=original.path1()) ++failures;
    copy.reset();if(observation::State.live || observation::State.blocks) ++failures;++persistent;
  }
  const auto overflow=BoundCatalogFilesystemErrorSource({SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX});
  report["overflow_rejected"]=overflow.overflow;if(!overflow.overflow) ++failures;
  {observation::Scope scope;void *extra=::operator new(copyBound.new_request_upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>copyBound.new_request_upper && observation::State.live==0;
  report["unmodeled_allocation_negative_control"]=negative?"PASS":"FAIL";if(!negative) ++failures;
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());
  if(resolved.parent_path()!=temp || resolved!=temp/parent.filename()) return 1;
  fs::remove_all(resolved);
  report["persistent_refusal_and_retry_cases"]=persistent;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="MSVC filesystem/system diagnostic path/message ordinary-new requests from declared lengths;real missing/not-directory/file_size/rename failures and constructor fixtures. Exception object/base C string,Win32 formatter scratch/CRT internals/caller input storage not observed or inferred as heap/RSS.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
#endif
}
