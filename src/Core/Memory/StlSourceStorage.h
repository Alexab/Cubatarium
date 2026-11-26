#pragma once

#include "Core/Memory/NameStorageSource.h"
#include <list>
#include <map>
#include <memory>
#include <type_traits>

namespace cutum
{
// Default allocator requests for the pinned Windows Release library. Node
// types come from the implementation. Large contiguous requests use its
// existing alignment surcharge. Inline container shells belong to callers.
class StlSourceStorage
{
public:
  NameStorageEnvelope bytes{0, SupportedProfile(), false};

  static constexpr bool SupportedProfile() noexcept
  {
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 && _MSVC_STL_UPDATE == 202604L && _ITERATOR_DEBUG_LEVEL == 0
    return true;
#else
    return false;
#endif
  }

  void Add(size_t amount) noexcept { NameStorageAdd(bytes, amount); }

  void Buffer(size_t count, size_t width) noexcept
  {
    Add(NameStorageBufferRequest(count, width, bytes));
  }

  template<class String> void Text(const String &text) noexcept
  {
    const auto object = reinterpret_cast<uintptr_t>(&text);
    const auto data = reinterpret_cast<uintptr_t>(text.data());
    if (data < object || data - object >= sizeof(text))
      Buffer(text.capacity() + 1, sizeof(typename String::value_type));
  }

  template<class Table> void Hash(const Table &table) noexcept
  {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    using Value = typename Table::value_type;
    static_assert(std::is_same_v<typename Table::allocator_type, std::allocator<Value>>);
    using Node = std::_List_node<Value, void *>;
    static_assert(sizeof(Node) < 4096);
    Add(sizeof(Node)); // sentinel
    if (table.size() > SIZE_MAX / sizeof(Node))
    {
      bytes.overflow = true;
      bytes.supported = false;
    }
    else
      Add(table.size() * sizeof(Node));
    Buffer(table.bucket_count(), 2 * sizeof(void *));
#else
    (void)table;
#endif
  }

  template<class Table> void Tree(const Table &table) noexcept
  {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    using Value = typename Table::value_type;
    static_assert(std::is_same_v<typename Table::allocator_type, std::allocator<Value>>);
    using Node = std::_Tree_node<Value, void *>;
    static_assert(sizeof(Node) < 4096);
    Add(sizeof(Node));
    if (table.size() > SIZE_MAX / sizeof(Node))
    {
      bytes.overflow = true;
      bytes.supported = false;
    }
    else
      Add(table.size() * sizeof(Node));
#else
    (void)table;
#endif
  }

  template<class T> void List(const std::list<T> &list) noexcept
  {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    using Node = std::_List_node<T, void *>;
    static_assert(sizeof(Node) < 4096);
    Add(sizeof(Node));
    if (list.size() > SIZE_MAX / sizeof(Node))
    {
      bytes.overflow = true;
      bytes.supported = false;
    }
    else
      Add(list.size() * sizeof(Node));
#else
    (void)list;
#endif
  }

  // The codec owns a tree, with no shared DOM subtrees. Use bounded recursion
  // and no diagnostic heap allocations. If depth exceeds the bound, report
  // unsupported instead of returning a purported total for a prefix.
  template<class Json> void JsonTree(const Json &value, unsigned depth = 0) noexcept
  {
    if (depth == 64)
    {
      bytes.supported = false;
      return;
    }
    if (value.is_object())
    {
      using Object = typename Json::object_t;
      const auto &object = value.template get_ref<const Object &>();
      Add(sizeof(Object));
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
      Tree(object);
      for (const auto &entry : object)
      {
        Text(entry.first);
        JsonTree(entry.second, depth + 1);
      }
#endif
    }
    else if (value.is_array())
    {
      using Array = typename Json::array_t;
      const auto &array = value.template get_ref<const Array &>();
      Add(sizeof(Array));
      Buffer(array.capacity(), sizeof(Json));
      for (const auto &item : array)
        JsonTree(item, depth + 1);
    }
    else if (value.is_string())
    {
      using String = typename Json::string_t;
      Add(sizeof(String));
      Text(value.template get_ref<const String &>());
    }
    else if (value.is_binary())
    {
      const auto &binary = value.get_binary();
      Add(sizeof(binary));
      Buffer(binary.capacity(), sizeof(typename Json::binary_t::value_type));
    }
  }
};
} // namespace cutum
