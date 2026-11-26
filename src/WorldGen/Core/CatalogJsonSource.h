#pragma once
#include <algorithm>
#include <array>
#include <istream>
#include <fstream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>

namespace cutum
{
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !JSON_DIAGNOSTIC_POSITIONS
inline constexpr bool kCatalogJsonSourceModelSupported=true;
#else
inline constexpr bool kCatalogJsonSourceModelSupported=false;
#endif
enum class CatalogJsonKind {Pack,Refs,Features,Prefab,Definitions,Manifest,ResourceBlocks,LegacyCube,Overrides,StatusEffects,ItemSurface,Count};
struct CatalogJsonSourceShape
{
  size_t bytes{},values{},objects{},arrays{},keys{},strings{},string_bytes{},max_depth{};
  size_t requested_heap_upper{};
  bool success{},overflow{},model_supported{};
  CatalogJsonKind kind{CatalogJsonKind::Pack};
};
struct CatalogJsonSourceSnapshot
{
  size_t active{},peak_active{},completed{},failed{},largest_input{},largest_requested_heap_upper{};
  std::array<size_t,static_cast<size_t>(CatalogJsonKind::Count)> attempts{};
  CatalogJsonSourceShape largest_bound_shape;
};
namespace detail
{
inline std::mutex CatalogJsonSourceMutex;
inline CatalogJsonSourceSnapshot CatalogJsonSources;
inline thread_local CatalogJsonSourceShape LastCatalogJsonSource;
inline void JsonSourceAdd(size_t &value,size_t amount,bool &overflow) noexcept
{
  if(amount>std::numeric_limits<size_t>::max()-value) {value=std::numeric_limits<size_t>::max();overflow=true;}
  else value+=amount;
}
inline void JsonSourceTerm(size_t &value,size_t count,size_t width,bool &overflow) noexcept
{
  if(width && count>std::numeric_limits<size_t>::max()/width) {value=std::numeric_limits<size_t>::max();overflow=true;}
  else JsonSourceAdd(value,count*width,overflow);
}
} // namespace detail

// Source/model envelope, NOT measured heap or a practical process/asset cap.
// Current MSVC Release only. Includes partial/overwritten source values and
// conservative simultaneous vector growth/destructor staging and error text.
inline CatalogJsonSourceShape BoundCatalogJsonSource(CatalogJsonSourceShape shape)
{
  shape.model_supported=false;shape.requested_heap_upper=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !JSON_DIAGNOSTIC_POSITIONS
  using Json=nlohmann::json;
  using Object=Json::object_t;
  using Node=std::_Tree_node<Object::value_type,void *>;
  shape.model_supported=true;
  size_t bound=0;
  detail::JsonSourceTerm(bound,shape.objects,sizeof(Object)+sizeof(Node),shape.overflow);
  detail::JsonSourceTerm(bound,shape.keys,sizeof(Node),shape.overflow);
  detail::JsonSourceTerm(bound,shape.arrays,sizeof(Json::array_t),shape.overflow);
  detail::JsonSourceTerm(bound,shape.strings,sizeof(Json::string_t),shape.overflow);
  detail::JsonSourceTerm(bound,shape.string_bytes,2,shape.overflow);
  detail::JsonSourceTerm(bound,shape.keys,32,shape.overflow);
  detail::JsonSourceTerm(bound,shape.strings,32,shape.overflow);
  // Array backings and JSON destroy()'s flattening vector: capacity/growth
  // allowance4*values each. Map value cells are already inside Node above.
  detail::JsonSourceTerm(bound,shape.values,8*sizeof(Json),shape.overflow);
  detail::JsonSourceTerm(bound,shape.max_depth,12*sizeof(Json *),shape.overflow);
  // vector<bool> parser states: at most4 times rounded32-bit word count.
  const size_t words=shape.max_depth/32+(shape.max_depth%32!=0);
  detail::JsonSourceTerm(bound,words,4*sizeof(unsigned int),shape.overflow);
  // Successful lexer: raw vector+decoded string and their growth overlap.
  // Failure: escaped token plus diagnostic concatenations, independently of
  // DOM backing/destruction. Fixed allowance is heap text, not native stack.
  detail::JsonSourceTerm(bound,shape.bytes,shape.success?8:64,shape.overflow);
  detail::JsonSourceAdd(bound,shape.success?512:16640,shape.overflow);
  shape.requested_heap_upper=bound;
#endif
  return shape;
}
inline auto GetCatalogJsonSourceSnapshot()
{std::lock_guard<std::mutex> lock(detail::CatalogJsonSourceMutex);return detail::CatalogJsonSources;}
inline auto GetLastCatalogJsonSource() {return detail::LastCatalogJsonSource;}

namespace detail
{
inline void RetireCatalogJson(nlohmann::json &root,std::vector<nlohmann::json*> &path) noexcept
{
  if(!root.is_structured()) {root=nullptr;return;}
  // Unsupported diagnostic-position builds keep the previous library policy.
  // Supported SAX parsing reserves this path before admitting each container.
  if(path.capacity()==0) return;
  path.clear();path.push_back(&root);
  while(!path.empty()) {
    auto &value=*path.back();
    if(value.is_array() && !value.empty()) {
      auto &array=value.get_ref<nlohmann::json::array_t&>();
      auto &child=array.back();
      if(child.is_structured() && !child.empty()) path.push_back(&child);
      else array.pop_back();
    } else if(value.is_object() && !value.empty()) {
      auto &object=value.get_ref<nlohmann::json::object_t&>();
      auto it=std::prev(object.end());
      if(it->second.is_structured() && !it->second.empty()) path.push_back(&it->second);
      else object.erase(it);
    } else {value=nullptr;path.pop_back();}
  }
}
struct CatalogJsonRecord
{
  CatalogJsonSourceShape &Shape;
  explicit CatalogJsonRecord(CatalogJsonSourceShape &shape):Shape(shape) {
    std::lock_guard<std::mutex> lock(CatalogJsonSourceMutex);
    ++CatalogJsonSources.active;CatalogJsonSources.peak_active=std::max(CatalogJsonSources.peak_active,CatalogJsonSources.active);
    const auto index=static_cast<size_t>(shape.kind);
    if(index<CatalogJsonSources.attempts.size()) {
      auto &attempts=CatalogJsonSources.attempts[index];
      if(attempts<std::numeric_limits<size_t>::max()) ++attempts;
    }
  }
  ~CatalogJsonRecord() {
    Shape=BoundCatalogJsonSource(Shape);LastCatalogJsonSource=Shape;
    std::lock_guard<std::mutex> lock(CatalogJsonSourceMutex);--CatalogJsonSources.active;
    auto &count=Shape.success?CatalogJsonSources.completed:CatalogJsonSources.failed;
    if(count<std::numeric_limits<size_t>::max()) ++count;
    CatalogJsonSources.largest_input=std::max(CatalogJsonSources.largest_input,Shape.bytes);
    if(Shape.model_supported && !Shape.overflow && Shape.requested_heap_upper>CatalogJsonSources.largest_requested_heap_upper) {
      CatalogJsonSources.largest_requested_heap_upper=Shape.requested_heap_upper;
      CatalogJsonSources.largest_bound_shape=Shape;
    }
  }
};
class CatalogJsonInput
{
  nlohmann::detail::input_stream_adapter Source;
  CatalogJsonSourceShape &Shape;
public:
  using char_type=char;
  CatalogJsonInput(std::istream &in,CatalogJsonSourceShape &shape):Source(in),Shape(shape) {}
  CatalogJsonInput(CatalogJsonInput &&)=default;
  auto get_character() {
    const auto value=Source.get_character();
    if(value!=std::char_traits<char>::eof()) JsonSourceAdd(Shape.bytes,1,Shape.overflow);
    return value;
  }
};
class CatalogJsonSax
{
  using Json=nlohmann::json;
  nlohmann::detail::json_sax_dom_parser<Json,CatalogJsonInput> Builder;
  CatalogJsonSourceShape &Shape;
  Json &Root;
  Json *Member{};
  std::vector<Json*> Containers,Retirement;
  size_t Depth{};
  void Value() {JsonSourceAdd(Shape.values,1,Shape.overflow);}
  void Text(const Json::string_t &value) {JsonSourceAdd(Shape.string_bytes,value.size(),Shape.overflow);}
  void Enter() {
    JsonSourceAdd(Depth,1,Shape.overflow);Shape.max_depth=std::max(Shape.max_depth,Depth);Value();
    for(auto *path:{&Containers,&Retirement}) if(Depth>path->capacity()) {
      const size_t cap=path->capacity(),extra=cap/2+1,limit=path->max_size();
      path->reserve(std::max(Depth,cap>limit-extra?limit:cap+extra));
    }
  }
  void Entered() {
    Json *value=Containers.empty()?&Root:Containers.back()->is_array()?&Containers.back()->back():Member;
    Containers.push_back(value);
  }
public:
  CatalogJsonSax(Json &root,CatalogJsonSourceShape &shape):Builder(root,true),Shape(shape),Root(root) {}
  void RetireRoot() noexcept {RetireCatalogJson(Root,Retirement);}
  std::vector<Json*> TakeRetirementPath() noexcept {return std::move(Retirement);}
  bool null() {Value();return Builder.null();}
  bool boolean(bool v) {Value();return Builder.boolean(v);}
  bool number_integer(Json::number_integer_t v) {Value();return Builder.number_integer(v);}
  bool number_unsigned(Json::number_unsigned_t v) {Value();return Builder.number_unsigned(v);}
  bool number_float(Json::number_float_t v,const Json::string_t &text) {Value();return Builder.number_float(v,text);}
  bool string(Json::string_t &v) {Value();JsonSourceAdd(Shape.strings,1,Shape.overflow);Text(v);return Builder.string(v);}
  bool binary(Json::binary_t &v) {Value();return Builder.binary(v);}
  bool start_object(size_t n) {Enter();JsonSourceAdd(Shape.objects,1,Shape.overflow);const bool ok=Builder.start_object(n);Entered();return ok;}
  bool key(Json::string_t &v) {
    JsonSourceAdd(Shape.keys,1,Shape.overflow);Text(v);
    auto &object=Containers.back()->get_ref<Json::object_t&>();
    const auto previous=object.find(v);
    if(previous!=object.end()) RetireCatalogJson(previous->second,Retirement);
    const bool ok=Builder.key(v);Member=&object.find(v)->second;return ok;
  }
  bool end_object() {--Depth;Containers.pop_back();return Builder.end_object();}
  bool start_array(size_t n) {Enter();JsonSourceAdd(Shape.arrays,1,Shape.overflow);const bool ok=Builder.start_array(n);Entered();return ok;}
  bool end_array() {--Depth;Containers.pop_back();return Builder.end_array();}
  template<class Exception> bool parse_error(size_t position,const std::string &token,const Exception &error)
  {return Builder.parse_error(position,token,error);}
};
} // namespace detail

// Concrete stack/value owner,never deleted polymorphically. Keep the cleanup
// path with the DOM until candidate construction and root retirement finish.
class CatalogJsonDocument:public nlohmann::json
{
  using Json=nlohmann::json;
  std::vector<Json*> Retirement;
public:
  CatalogJsonDocument()=default;
  CatalogJsonDocument(Json &&root,std::vector<Json*> &&retirement) noexcept:Json(std::move(root)),Retirement(std::move(retirement)) {}
  CatalogJsonDocument(const CatalogJsonDocument &)=delete;
  CatalogJsonDocument &operator=(const CatalogJsonDocument &)=delete;
  CatalogJsonDocument(CatalogJsonDocument &&other) noexcept:Json(std::move(static_cast<Json&>(other))),Retirement(std::move(other.Retirement)) {}
  CatalogJsonDocument &operator=(CatalogJsonDocument &&other) noexcept {
    if(this!=&other) {detail::RetireCatalogJson(*this,Retirement);Json::operator=(std::move(static_cast<Json&>(other)));Retirement=std::move(other.Retirement);}return *this;
  }
  ~CatalogJsonDocument() {detail::RetireCatalogJson(*this,Retirement);}
  Json ReleaseRoot() noexcept {return std::move(static_cast<Json&>(*this));}
  size_t RetirementCapacity() const noexcept {return Retirement.capacity();}
  size_t RetirementRequestedBytes() const noexcept {
    size_t bytes=Retirement.capacity()*sizeof(Json*);
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    if(bytes>=std::_Big_allocation_threshold) bytes+=std::_Non_user_size;
#endif
    return bytes;
  }
};
inline CatalogJsonDocument ParseCatalogSourceDocument(std::istream &in,CatalogJsonKind kind)
{
  CatalogJsonSourceShape shape;shape.kind=kind;
  detail::CatalogJsonRecord record(shape);
  nlohmann::json root;
  nlohmann::detail::parser<nlohmann::json,detail::CatalogJsonInput> parser{detail::CatalogJsonInput(in,shape)};
#if JSON_DIAGNOSTIC_POSITIONS
  // Preserve library lexer position metadata; this source model is unsupported
  // in that configuration, rather than silently losing positions.
  parser.parse(true,root);
  shape.success=true;
  return CatalogJsonDocument{std::move(root),{}};
#else
  detail::CatalogJsonSax sax(root,shape);
  try {parser.sax_parse(&sax,true);} catch(...) {sax.RetireRoot();throw;}
  shape.success=true;
  return CatalogJsonDocument{std::move(root),sax.TakeRetirementPath()};
#endif
}
inline nlohmann::json ParseCatalogSourceJson(std::istream &in,CatalogJsonKind kind)
{return ParseCatalogSourceDocument(in,kind).ReleaseRoot();}
// Catalog callers own a fresh ifstream and do not read it after JSON parsing.
// Release its FILE/OS handle and read buffer before application candidate
// construction,including exceptional exits. Match filebuf destruction's
// non-throwing close policy; preserve the original parser exception/result.
inline CatalogJsonDocument ParseAndCloseCatalogFileJson(std::ifstream &file,CatalogJsonKind kind)
{
  struct CloseFile {
    std::ifstream &File;
    ~CloseFile() noexcept {try {File.close();} catch(...) {}}
  } close{file};
  return ParseCatalogSourceDocument(file,kind);
}
} // namespace cutum
