#pragma once
#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>

namespace cutum
{
struct CatalogFilesystemLayout
{
  size_t simple_shared_block{},recursive_shared_block{},handle_cell{};
  size_t iterator_inline{},recursive_iterator_inline{},entry_inline{},creator_inline{},find_data_inline{};
  bool supported{};
};
inline CatalogFilesystemLayout GetCatalogFilesystemLayout() noexcept
{
  CatalogFilesystemLayout result;
  result.iterator_inline=sizeof(std::filesystem::directory_iterator);
  result.recursive_iterator_inline=sizeof(std::filesystem::recursive_directory_iterator);
  result.entry_inline=sizeof(std::filesystem::directory_entry);
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && defined(_WIN64)
  result.supported=true;
  result.simple_shared_block=sizeof(std::_Ref_count_obj2<std::filesystem::_Dir_enum_impl>);
  result.recursive_shared_block=sizeof(std::_Ref_count_obj2<std::filesystem::_Recursive_dir_enum_impl>);
  result.handle_cell=sizeof(std::filesystem::_Find_file_handle);
  result.creator_inline=sizeof(std::filesystem::_Dir_enum_impl::_Creator);
  result.find_data_inline=sizeof(__std_fs_find_data);
#endif
  return result;
}
struct CatalogFilesystemDomain
{
  // Maximum FULL native UTF16 path length encountered/constructed by this
  // scan,including root and empty directories;not current short entry length.
  size_t max_path_units{};
  // Directory descents below root,including attempted empty directories.
  // A deepest empty directory can be opened without yielding an entry at that
  // depth,so iterator.depth() samples alone do not establish this domain.
  size_t max_directory_depth{},prefix_bytes{};
  bool recursive{};
};
struct CatalogFilesystemEnvelope
{
  size_t traversal_heap_upper{},name_operations_heap_upper{},combined_heap_upper{},search_handles_upper{};
  bool supported{},overflow{};
};
// One independent scan. Copied input iterators share its implementation and
// search handles;copies' inline shared_ptr slots are separate bookkeeping.
// Current MSVC Release ordinary-new source requests only. Windows internal
// allocations/OS handle storage,CRT/locale,filesystem_error formatting,caller
// input path/prefix capacities and compiled catalog candidates remain separate.
inline CatalogFilesystemEnvelope BoundCatalogFilesystemSource(const CatalogFilesystemDomain &domain)
{
  CatalogFilesystemEnvelope result;const auto layout=GetCatalogFilesystemLayout();result.supported=layout.supported;
  const auto add=[&](size_t a,size_t b) {
    if(b>std::numeric_limits<size_t>::max()-a) {result.overflow=true;return std::numeric_limits<size_t>::max();}return a+b;
  };
  const auto mul=[&](size_t a,size_t b) {
    if(b && a>std::numeric_limits<size_t>::max()/b) {result.overflow=true;return std::numeric_limits<size_t>::max();}return a*b;
  };
  // Fresh canonical copies/string conversions:capacity rounding and growth,
  // including simultaneous old/new buffers,are covered by these allowances.
  const auto text=[&](size_t chars,size_t width) {return add(mul(mul(add(chars,16),2),width),39);};
  const size_t native=text(domain.max_path_units,sizeof(wchar_t));
  // _Creator/_Open_dir original path,current entry old/new and filename
  // replacement temp:at most4 native backings. Wildcard+roundup fits+16.
  result.traversal_heap_upper=add(domain.recursive?layout.recursive_shared_block:layout.simple_shared_block,mul(native,4));
  result.search_handles_upper=domain.recursive?add(domain.max_directory_depth,1):1;
  if(domain.recursive) {
    // _Recursive_dir_enum_impl stack owns handles,not saved full path strings.
    // MSVC geometric vector live old+new <=4*(D+1) cells;alignment allowance.
    result.traversal_heap_upper=add(result.traversal_heap_upper,
      add(mul(mul(add(domain.max_directory_depth,1),4),layout.handle_cell),78));
  }
  // Selected catalog scan operations:extension(),stem().string(),full .string()
  // and MakeQualifiedBlockName(prefix,local). Four native temp backings and
  // four narrow old/new backings conservatively coexist. UTF8/Windows codepage
  // conversion <=4 narrow bytes per native unit. Prefix data is caller-owned.
  const size_t name_bytes=add(add(mul(domain.max_path_units,4),domain.prefix_bytes),2);
  result.name_operations_heap_upper=add(mul(native,4),mul(text(name_bytes,1),4));
  result.combined_heap_upper=add(result.traversal_heap_upper,result.name_operations_heap_upper);
  return result;
}
} // namespace cutum
