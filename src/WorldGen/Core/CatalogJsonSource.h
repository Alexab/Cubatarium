#pragma once
#include <algorithm>
#include <array>
#include <istream>
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
enum class CatalogJsonKind {Pack,Refs,Features,Prefab,Definitions,Manifest,ResourceBlocks,LegacyCube,Count};
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
  detail::JsonSourceTerm(bound,shape.max_depth,4*sizeof(Json *),shape.overflow);
  // vector<bool> parser states: at most4 times rounded32-bit word count.
  const size_t words=shape.max_depth/32+(shape.max_depth%32!=0);
  detail::JsonSourceTerm(bound,words,4*sizeof(unsigned int),shape.overflow);
  // Successful lexer: raw vector+decoded string and their growth overlap.
  // Failure: escaped token plus diagnostic concatenations, independently of
  // DOM backing/destruction. Fixed allowance is heap text, not native stack.
  detail::JsonSourceTerm(bound,shape.bytes,shape.success?8:64,shape.overflow);
  detail::JsonSourceAdd(bound,shape.success?256:16384,shape.overflow);
  shape.requested_heap_upper=bound;
#endif
  return shape;
}
inline auto GetCatalogJsonSourceSnapshot()
{std::lock_guard<std::mutex> lock(detail::CatalogJsonSourceMutex);return detail::CatalogJsonSources;}
inline auto GetLastCatalogJsonSource() {return detail::LastCatalogJsonSource;}

namespace detail
{
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
  size_t Depth{};
  void Value() {JsonSourceAdd(Shape.values,1,Shape.overflow);}
  void Text(const Json::string_t &value) {JsonSourceAdd(Shape.string_bytes,value.size(),Shape.overflow);}
  void Enter() {JsonSourceAdd(Depth,1,Shape.overflow);Shape.max_depth=std::max(Shape.max_depth,Depth);Value();}
public:
  CatalogJsonSax(Json &root,CatalogJsonSourceShape &shape):Builder(root,true),Shape(shape) {}
  bool null() {Value();return Builder.null();}
  bool boolean(bool v) {Value();return Builder.boolean(v);}
  bool number_integer(Json::number_integer_t v) {Value();return Builder.number_integer(v);}
  bool number_unsigned(Json::number_unsigned_t v) {Value();return Builder.number_unsigned(v);}
  bool number_float(Json::number_float_t v,const Json::string_t &text) {Value();return Builder.number_float(v,text);}
  bool string(Json::string_t &v) {Value();JsonSourceAdd(Shape.strings,1,Shape.overflow);Text(v);return Builder.string(v);}
  bool binary(Json::binary_t &v) {Value();return Builder.binary(v);}
  bool start_object(size_t n) {Enter();JsonSourceAdd(Shape.objects,1,Shape.overflow);return Builder.start_object(n);}
  bool key(Json::string_t &v) {JsonSourceAdd(Shape.keys,1,Shape.overflow);Text(v);return Builder.key(v);}
  bool end_object() {--Depth;return Builder.end_object();}
  bool start_array(size_t n) {Enter();JsonSourceAdd(Shape.arrays,1,Shape.overflow);return Builder.start_array(n);}
  bool end_array() {--Depth;return Builder.end_array();}
  template<class Exception> bool parse_error(size_t position,const std::string &token,const Exception &error)
  {return Builder.parse_error(position,token,error);}
};
} // namespace detail

inline nlohmann::json ParseCatalogSourceJson(std::istream &in,CatalogJsonKind kind)
{
  CatalogJsonSourceShape shape;shape.kind=kind;
  detail::CatalogJsonRecord record(shape);
  nlohmann::json root;
  nlohmann::detail::parser<nlohmann::json,detail::CatalogJsonInput> parser{detail::CatalogJsonInput(in,shape)};
#if JSON_DIAGNOSTIC_POSITIONS
  // Preserve library lexer position metadata; this source model is unsupported
  // in that configuration, rather than silently losing positions.
  parser.parse(true,root);
#else
  detail::CatalogJsonSax sax(root,shape);
  parser.sax_parse(&sax,true);
#endif
  shape.success=true;
  return root;
}
} // namespace cutum
