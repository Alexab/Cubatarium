#pragma once
#include "WorldGen/Core/CatalogFilesystemSourceEnvelope.h"
#include "ResourcePacks/BlockNameUtil.h"

static int RunCatalogFilesystemSourceFixture(const char *reportPath)
{
  namespace fs=std::filesystem;
  Json report={{"schema","catalog-filesystem-source-requests.v1"},{"status","PASS"},{"cases",Json::array()}};
#if !defined(_WIN32)
  report["status"]="UNTESTED";report["reason"]="Windows MSVC source/handle oracle required";
  if(reportPath){std::ofstream file(reportPath);file<<report.dump(2)<<'\n';}return 2;
#else
  size_t failures=0,persistent=0;
  const auto layout=GetCatalogFilesystemLayout();
  report["layout"]={{"supported",layout.supported},{"simple_shared_block",layout.simple_shared_block},
    {"recursive_shared_block",layout.recursive_shared_block},{"handle_cell",layout.handle_cell},
    {"iterator_inline",layout.iterator_inline},{"recursive_iterator_inline",layout.recursive_iterator_inline},
    {"entry_inline",layout.entry_inline},{"creator_inline",layout.creator_inline},{"find_data_inline",layout.find_data_inline}};
  if(!layout.supported) return 2;
  const auto parent=fs::temp_directory_path()/(L"cuba_fs_"+std::to_wstring(GetCurrentProcessId()));
  const auto flat=parent/L"flat",deep=parent/L"deep",empty=parent/L"empty";
  fs::create_directories(flat);fs::create_directories(deep);fs::create_directories(empty);
  size_t flatWidth=flat.native().size(),deepWidth=deep.native().size();
  const auto create=[&](const fs::path &file,size_t &width) {width=std::max(width,file.native().size());std::ofstream out(file);out<<"{}";};
  for(int i=0;i<80;++i) create(flat/(L"item_"+std::to_wstring(i)+L".json"),flatWidth);
  create(flat/(std::wstring(150,L'x')+L".json"),flatWidth);
  auto level=deep;
  for(int i=0;i<18;++i) {level/=L"d"+std::to_wstring(i);fs::create_directory(level);create(level/(L"item_"+std::to_wstring(i)+L".json"),deepWidth);}
  fs::create_directory(level/L"empty");deepWidth=std::max(deepWidth,(level/L"empty").native().size());
  const auto unicode=deep/L"данные";fs::create_directory(unicode);create(unicode/L"entry.json",deepWidth);
  // Warm platform/codepage paths before isolated handle/request observations.
  (void)unicode.string();{fs::recursive_directory_iterator warm(deep);for(const auto &entry:warm)(void)entry.is_regular_file();}
  const auto handles=[] {DWORD count=0;GetProcessHandleCount(GetCurrentProcess(),&count);return count;};
  const auto scan=[&](const fs::path &root,const CatalogFilesystemDomain &domain,const std::string &prefix,size_t failAfter,bool names,
                      size_t expectedCount,Json *row) {
    const auto bound=BoundCatalogFilesystemSource(domain);const DWORD before=handles();DWORD maximum=before;
    bool refused=false,copyNoAllocation=true,exactShared=true,lastCopyOwns=true;size_t visited=0;
    {
      observation::Scope scope(failAfter);
      try {
        const auto walk=[&](auto it,auto end) {
          const auto attempts=observation::State.attempts;auto copy=it;
          copyNoAllocation=observation::State.attempts==attempts;
          if(it!=end) {
            observation::State.active=false;
            const fs::path *path[]={&it->path()};const auto storage=MeasureSourceInputStorage(nullptr,0,nullptr,0,path,1);
            const size_t shared=domain.recursive?layout.recursive_shared_block:layout.simple_shared_block;
            exactShared=observation::State.live==shared+storage.RequestedHeapComponents();
            observation::State.active=true;
          }
          for(;it!=end;++it) {
            ++visited;maximum=std::max(maximum,handles());
            if(names) {
              const auto extension=it->path().extension();const auto local=it->path().stem().string();
              const auto qualified=MakeQualifiedBlockName(prefix,local);const auto full=it->path().string();
              if(qualified.size()!=prefix.size()+2+local.size() || full.empty()) ++failures;
              (void)extension;
            }
          }
          if(visited) lastCopyOwns=observation::State.live!=0;
          copy=decltype(copy){};
        };
        if(domain.recursive) walk(fs::recursive_directory_iterator(root),fs::recursive_directory_iterator{});
        else walk(fs::directory_iterator(root),fs::directory_iterator{});
      }catch(const std::bad_alloc &){refused=true;}
    }
    const auto counts=observation::State;const DWORD after=handles();
    const size_t upper=names?bound.combined_heap_upper:bound.traversal_heap_upper;
    const bool ok=bound.supported && !bound.overflow && counts.live==0 && counts.blocks==0 && counts.peak<=upper &&
      maximum-before<=bound.search_handles_upper && before==after && copyNoAllocation && lastCopyOwns &&
      (failAfter==SIZE_MAX?!refused&&visited==expectedCount&&exactShared:refused);
    if(!ok) ++failures;
    if(row) *row={{"pass",ok},{"peak",counts.peak},{"source_upper",upper},{"attempts",counts.attempts},
      {"visited",visited},{"final_live",counts.live},{"handle_delta_max",maximum-before},{"search_handle_upper",bound.search_handles_upper},
      {"handle_count_restored",before==after},{"copy_allocates",!copyNoAllocation},{"last_copy_retains_implementation",lastCopyOwns},
      {"shared_block_and_path_exact",exactShared}};
    return counts.attempts;
  };
  const std::string prefix(8193,'p');
  struct Input {const char *name;fs::path root;CatalogFilesystemDomain domain;size_t entries;bool names;};
  const std::vector<Input> inputs={
    {"empty_flat",empty,{empty.native().size(),0,0,false},0,false},
    {"flat81",flat,{flatWidth,0,0,false},81,false},
    {"flat_names_large_prefix",flat,{flatWidth,0,prefix.size(),false},81,true},
    {"deep18_empty19_unicode",deep,{deepWidth,19,0,true},39,false},
    {"deep_names_large_prefix",deep,{deepWidth,19,prefix.size(),true},39,true}};
  for(const auto &input:inputs) {
    Json row;const size_t attempts=scan(input.root,input.domain,prefix,SIZE_MAX,input.names,input.entries,&row);
    row["name"]=input.name;report["cases"].push_back(row);
    // Every persistent ordinary-new boundary for full normal traversal+names.
    if(input.names) for(size_t i=0;i<attempts;++i) {scan(input.root,input.domain,prefix,i,true,input.entries,nullptr);scan(input.root,input.domain,prefix,SIZE_MAX,true,input.entries,nullptr);++persistent;}
  }
  // A const source path may have arbitrary unused capacity;iterator's fresh
  // canonical copy follows source length,not the caller's reserve history.
  std::wstring spare=flat.native();spare.reserve(16385);const fs::path spareRoot(std::move(spare));
  Json spareRow;scan(spareRoot,{flatWidth,0,0,false},prefix,SIZE_MAX,false,81,&spareRow);spareRow["name"]="input_spare_capacity_not_copied";report["cases"].push_back(spareRow);
  const auto overflow=BoundCatalogFilesystemSource({SIZE_MAX,SIZE_MAX,SIZE_MAX,true});
  report["overflow_rejected"]=overflow.overflow;if(!overflow.overflow) ++failures;
  const auto flatBound=BoundCatalogFilesystemSource({flatWidth,0,prefix.size(),false});
  {observation::Scope scope;void *extra=::operator new(flatBound.combined_heap_upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>flatBound.combined_heap_upper && observation::State.live==0;
  report["unmodeled_allocation_negative_control"]=negative?"PASS":"FAIL";if(!negative) ++failures;
  report["persistent_refusal_and_retry_cases"]=persistent;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  // All target paths resolved directly under this unique fixture parent.
  const auto resolved=fs::weakly_canonical(parent);const auto temp=fs::weakly_canonical(fs::temp_directory_path());
  if(resolved.parent_path()!=temp || resolved!=temp/parent.filename()) return 1;
  fs::remove_all(resolved);
  report["scope"]="Pinned MSVC valid directory scans/name operations and persistent ordinary-new refusal cleanup;input/candidate payload,localized filesystem_error text,CRT/OS internals and total process memory separate. D includes attempted empty directories. No production traversal algorithm changed.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
#endif
}
