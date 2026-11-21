#include "WorldGen/Core/CatalogJsonSource.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

// Isolated executable only. Every modeled JSON allocation has ordinary
// alignment; MSVC's manual vector alignment still requests ordinary new.
// Track requested new bytes, excluding this observer prefix and direct CRT
// malloc/exception/runtime/stream allocations. No production allocator change.
namespace observation
{
struct Counts {size_t epoch{},live{},blocks{},peak{},allocations{};bool active{};};
inline Counts State;
struct alignas(std::max_align_t) Header {size_t bytes,epoch;};
constexpr size_t Prefix=(sizeof(Header)+15)/16*16;
void *Allocate(size_t bytes) {
  if(bytes>std::numeric_limits<size_t>::max()-Prefix) throw std::bad_alloc{};
  auto *memory=static_cast<unsigned char *>(std::malloc(Prefix+std::max(bytes,size_t{1})));
  if(!memory) throw std::bad_alloc{};
  const size_t epoch=State.active?State.epoch:0;
  ::new(memory) Header{bytes,epoch};
  if(epoch) {State.live+=bytes;++State.blocks;++State.allocations;State.peak=std::max(State.peak,State.live);}
  return memory+Prefix;
}
void Free(void *pointer) noexcept {
  if(!pointer) return;
  auto *memory=static_cast<unsigned char *>(pointer)-Prefix;
  const auto header=*reinterpret_cast<const Header *>(memory);std::free(memory);
  if(header.epoch && header.epoch==State.epoch) {State.live-=header.bytes;--State.blocks;}
}
struct Scope {
  Scope() {
    if(State.active || State.live || State.blocks) std::abort();
    const size_t epoch=State.epoch+1;State={};State.epoch=epoch;State.active=true;
  }
  ~Scope(){State.active=false;}
};
} // namespace observation
void *operator new(size_t n) {return observation::Allocate(n);}
void *operator new[](size_t n) {return observation::Allocate(n);}
void operator delete(void *p) noexcept {observation::Free(p);}
void operator delete[](void *p) noexcept {observation::Free(p);}
void operator delete(void *p,size_t) noexcept {observation::Free(p);}
void operator delete[](void *p,size_t) noexcept {observation::Free(p);}
void *operator new(size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t &) noexcept {observation::Free(p);}
void operator delete[](void *p,const std::nothrow_t &) noexcept {observation::Free(p);}

static void Check(bool value,const char *message)
{if(!value) {std::fprintf(stderr,"catalog_json_source_envelope_test: %s\n",message);std::abort();}}

int main()
{
  using Json=nlohmann::json;using namespace cutum;
  static_assert(alignof(Json)<=16 && alignof(Json::object_t::value_type)<=16);
  if(!kCatalogJsonSourceModelSupported) {
    std::cout << R"({"status":"UNTESTED","reason":"source_model_requires_current_MSVC_Release"})" << std::endl;return 2;
  }
  std::vector<std::pair<std::string,std::string>> cases={
      {"null","null"},{"empty_object","{}"},{"empty_array","[]"},
      {"scalar_string",R"("abc")"},{"duplicate_subtree",R"({"x":[1,{"s":"long"}],"x":[2,3]})"},
      {"BOM_unicode","\xEF\xBB\xBF{\"x\":\"\\uD83D\\uDE03\"}"},
      {"deep",std::string(1024,'[')+"0"+std::string(1024,']')},
      {"long_string","\""+std::string(256*1024,'s')+"\""},
      {"overwritten_string","{\"x\":\""+std::string(256*1024,'x')+"\",\"x\":\"s\"}"},
      {"bad_object","{"},{"trailing","[1,2] 0"},{"bad_surrogate",R"("\uD800")"},
      {"long_bad_string","\""+std::string(65536,'q')+std::string(1,'\x01')+"\""},
      {"overflow_number",std::string(65536,'9')}};
  std::string array="[",object="{";
  for(int i=0;i<20000;++i) {if(i) array+=',';array+=std::to_string(i);}array+=']';
  for(int i=0;i<2000;++i) {if(i) object+=',';object+='"'+std::string(64,'k')+std::to_string(i)+"\":\""+std::string(80,'v')+'"';}object+='}';
  cases.emplace_back("wide_array",std::move(array));cases.emplace_back("wide_map",std::move(object));
  Json report={{"schema","catalog-json-new-request-verification.v1"},{"status","PASS"},{"cases",Json::array()}};
  for(const auto &[name,text]:cases) {
    std::istringstream expectedInput(text);Json expected;std::string expectedError;
    try {expected=Json::parse(expectedInput);} catch(const Json::exception &e) {expectedError=e.what();}
    std::istringstream input(text);bool equivalent=false;CatalogJsonSourceShape shape;
    {
      observation::Scope scope;
      try {
        const auto actual=ParseCatalogSourceJson(input,CatalogJsonKind::Pack);
        equivalent=expectedError.empty() && actual==expected;
      } catch(const Json::exception &e) {equivalent=!expectedError.empty() && std::strcmp(e.what(),expectedError.c_str())==0;}
      shape=GetLastCatalogJsonSource();
    }
    Check(equivalent && input.rdstate()==expectedInput.rdstate(),"value/error/stream differs from original parser");
    Check(!observation::State.live && !observation::State.blocks,"parser/destructor left observed allocations alive");
    Check(shape.model_supported && !shape.overflow && observation::State.peak<=shape.requested_heap_upper,"observed new requests exceed source model");
    report["cases"].push_back({{"name",name},{"success",shape.success},{"bytes",shape.bytes},
        {"values",shape.values},{"depth",shape.max_depth},{"observed_new_request_peak",observation::State.peak},
        {"source_model_upper",shape.requested_heap_upper},{"allocations",observation::State.allocations},{"final_live",0}});
  }
  bool detectsExtra=false;
  {
    std::istringstream input("[1,2,3]");observation::Scope scope;
    {const auto value=ParseCatalogSourceJson(input,CatalogJsonKind::Pack);Check(value.size()==3,"negative control input invalid");}
    const auto shape=GetLastCatalogJsonSource();void *extra=::operator new(shape.requested_heap_upper+4096);
    detectsExtra=observation::State.peak>shape.requested_heap_upper;::operator delete(extra);
  }
  Check(detectsExtra && !observation::State.live && !observation::State.blocks,"negative extra allocation was not detected/retired");
  report["unmodeled_allocation_negative_control"]=true;
  report["scope"]="Replaceable ordinary new requests through parser/DOM destruction only; observer prefix/input streams/expected DOM/direct CRT/native stack excluded; not total heap/RSS";
  std::cout << report.dump(2) << std::endl;
}
