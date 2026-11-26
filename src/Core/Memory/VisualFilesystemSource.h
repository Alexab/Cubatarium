#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include "WorldGen/Core/CatalogFilesystemSourceEnvelope.h"
#include <array>
#include <cstdlib>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cutum
{
namespace fs = std::filesystem;

enum class VisualFilesystemOp
{
  Absolute, WeakCanonical, LexicalNormal, LexicalRelative, GenericString, NativeString, FlatScan, Count
};

inline constexpr std::array VisualFilesystemNames{
    "absolute", "weakly_canonical", "lexically_normal", "lexically_relative",
    "generic_string", "string", "flat_scan"};

// Ordinary requests from the pinned Windows STL. Input storage, the Windows
// implementation of the queries, and exception formatting have their own
// owners. The query limit is the unsigned 32-bit result of the Windows ABI,
// including a possible retry for a larger buffer.
inline NameStorageEnvelope BoundVisualFilesystemSource(VisualFilesystemOp operation,
    size_t inputUnits, size_t baseUnits = 0) noexcept
{
  NameStorageEnvelope total{0, StlSourceStorage::SupportedProfile() && sizeof(wchar_t) == 2, false};
  if (!total.supported)
    return total;

  const auto sum = [&](size_t a, size_t b) {
    NameStorageEnvelope value{a, true, false};
    NameStorageAdd(value, b);
    total.overflow |= value.overflow;
    return value.requested;
  };
  const auto product = [&](size_t a, size_t b) {
    if (b && a > SIZE_MAX / b)
    {
      total.overflow = true;
      return SIZE_MAX;
    }
    return a * b;
  };
  const auto addBacking = [&](size_t count, size_t width, size_t live) {
    const auto request = NameStorageBufferRequest(count, width, total);
    NameStorageAdd(total, product(request, live));
  };
  const auto text = [&](size_t units, size_t width, size_t live) {
    // Before growth old capacity is below the new length. Allow the 1.5x
    // geometry, allocator rounding, the terminator and any old/new pair.
    addBacking(sum(sum(units, units / 2), 16 / width), width, live);
  };
  const auto normal = [&] {
    const auto tokens = std::max(size_t{19}, inputUnits);
    addBacking(sum(tokens, tokens / 2), sizeof(std::wstring_view), 2);
    text(sum(inputUnits, 1), sizeof(wchar_t), 2);
  };
  constexpr size_t queryUnits = UINT32_MAX;
  constexpr size_t canonicalPrefixUnits = 14; // "\\?\GLOBALROOT" on the NT route
  switch (operation)
  {
    case VisualFilesystemOp::Absolute:
      text(queryUnits, sizeof(wchar_t), 2);
      break;
    case VisualFilesystemOp::WeakCanonical:
      // Missing suffix may keep the last candidate, normalized path and
      // relative path while a resolved prefix and appended suffix coexist.
      normal();
      text(sum(inputUnits, 1), sizeof(wchar_t), 2);
      text(sum(sum(queryUnits, canonicalPrefixUnits + 2), inputUnits), sizeof(wchar_t), 4);
      break;
    case VisualFilesystemOp::LexicalNormal:
      if (inputUnits)
        normal();
      break;
    case VisualFilesystemOp::LexicalRelative:
      // UNC relative copies, root-name temporaries, path iterator values
      // and their algorithm copies, and result growth. Each unmatched base
      // element can contribute at most one ".." plus a separator.
      text(sum(sum(inputUnits, product(baseUnits, 3)), 2), sizeof(wchar_t), 24);
      break;
    case VisualFilesystemOp::GenericString:
      text(inputUnits, sizeof(wchar_t), 1); // slash replacement backing
      [[fallthrough]];
    case VisualFilesystemOp::NativeString:
    {
      // The STL selects UTF-8 or the current ANSI/OEM file code page.
      size_t bytesPerUnit = 4;
#ifdef _WIN32
      static const size_t hostWidth = []() -> size_t {
        CPINFO ansi{}, oem{};
        if (!GetCPInfo(CP_ACP, &ansi) || !GetCPInfo(CP_OEMCP, &oem))
          return 0;
        return std::max({4u, ansi.MaxCharSize, oem.MaxCharSize});
      }();
      bytesPerUnit = hostWidth;
#endif
      total.supported &= bytesPerUnit != 0;
      // WideCharToMultiByte's output length in this STL has type int.
      text(std::min(product(inputUnits, bytesPerUnit), static_cast<size_t>(INT32_MAX)), sizeof(char), 1);
      break;
    }
    case VisualFilesystemOp::FlatScan:
    {
      // FindFirstFileExW/FindNextFileW return a single name in a fixed-size
      // array. The iterator replaces that filename under the same root.
#if defined(_MSVC_STL_VERSION) && defined(_WIN64)
      constexpr size_t nameUnits = sizeof(__std_fs_find_data::_File_name) / sizeof(wchar_t);
      const auto domain = BoundCatalogFilesystemSource({sum(inputUnits, nameUnits + 1), 0, 0, false});
      NameStorageAdd(total, domain.combined_heap_upper);
      total.overflow |= domain.overflow;
      total.supported &= domain.supported;
#endif
      break;
    }
    case VisualFilesystemOp::Count:
      total.supported = false;
      break;
  }
  total.supported &= !total.overflow;
  return total;
}

struct VisualFilesystemSourceRow
{
  size_t calls{}, finished{}, failed{}, active{}, peakActive{}, unsupported{};
  size_t inputUnitsMax{}, outputUnitsMax{}, outputBackingMax{}, internalUpperMax{};
};

using VisualFilesystemSnapshot =
    std::array<VisualFilesystemSourceRow, static_cast<size_t>(VisualFilesystemOp::Count)>;

namespace visual_filesystem_detail
{
inline std::mutex Mutex;
inline VisualFilesystemSnapshot Rows;
} // namespace visual_filesystem_detail

inline VisualFilesystemSnapshot GetVisualFilesystemSources()
{
  const std::lock_guard lock(visual_filesystem_detail::Mutex);
  return visual_filesystem_detail::Rows;
}

class VisualFilesystemSource
{
public:
  VisualFilesystemSource(VisualFilesystemOp operation, size_t inputUnits, size_t baseUnits = 0)
  {
    static const bool enabled = std::getenv("CUBA_CATALOG_RUNTIME_DOMAIN") != nullptr;
    if (!enabled)
      return;
    using namespace visual_filesystem_detail;
    Row = &Rows[static_cast<size_t>(operation)];
    const auto bound = BoundVisualFilesystemSource(operation, inputUnits, baseUnits);
    const std::lock_guard lock(Mutex);
    ++Row->calls;
    Row->peakActive = std::max(Row->peakActive, ++Row->active);
    Row->unsupported += !bound.supported;
    Row->inputUnitsMax = std::max({Row->inputUnitsMax, inputUnits, baseUnits});
    Row->internalUpperMax = std::max(Row->internalUpperMax, bound.requested);
  }

  VisualFilesystemSource(const VisualFilesystemSource &) = delete;
  VisualFilesystemSource &operator=(const VisualFilesystemSource &) = delete;

  ~VisualFilesystemSource()
  {
    if (!Row)
      return;
    const std::lock_guard lock(visual_filesystem_detail::Mutex);
    --Row->active;
    Row->finished += Finished;
    Row->failed += !Finished;
    Row->outputUnitsMax = std::max(Row->outputUnitsMax, OutputUnits);
    Row->outputBackingMax = std::max(Row->outputBackingMax, OutputBacking);
    Row->unsupported += !OutputSupported;
  }

  void Finish() noexcept { Finished = true; }

  template<class Text> void Output(const Text &text) noexcept
  {
    if (!Row)
      return;
    StlSourceStorage backing;
    backing.Text(text);
    OutputUnits = std::max(OutputUnits, text.size());
    OutputBacking = std::max(OutputBacking, backing.bytes.requested);
    OutputSupported &= backing.bytes.supported;
  }

private:
  VisualFilesystemSourceRow *Row{};
  size_t OutputUnits{}, OutputBacking{};
  bool Finished{}, OutputSupported{true};
};

inline fs::path VisualAbsolute(const fs::path &input)
{
  VisualFilesystemSource source(VisualFilesystemOp::Absolute, input.native().size());
  auto result = fs::absolute(input);
  source.Output(result.native());
  source.Finish();
  return result;
}

inline fs::path VisualWeakCanonical(const fs::path &input)
{
  VisualFilesystemSource source(VisualFilesystemOp::WeakCanonical, input.native().size());
  auto result = fs::weakly_canonical(input);
  source.Output(result.native());
  source.Finish();
  return result;
}

inline fs::path VisualLexicalNormal(const fs::path &input)
{
  VisualFilesystemSource source(VisualFilesystemOp::LexicalNormal, input.native().size());
  auto result = input.lexically_normal();
  source.Output(result.native());
  source.Finish();
  return result;
}

inline fs::path VisualLexicalRelative(const fs::path &input, const fs::path &base)
{
  VisualFilesystemSource source(VisualFilesystemOp::LexicalRelative, input.native().size(), base.native().size());
  auto result = input.lexically_relative(base);
  source.Output(result.native());
  source.Finish();
  return result;
}

inline std::string VisualGenericString(const fs::path &input)
{
  VisualFilesystemSource source(VisualFilesystemOp::GenericString, input.native().size());
  auto result = input.generic_string();
  source.Output(result);
  source.Finish();
  return result;
}

inline std::string VisualNativeString(const fs::path &input)
{
  VisualFilesystemSource source(VisualFilesystemOp::NativeString, input.native().size());
  auto result = input.string();
  source.Output(result);
  source.Finish();
  return result;
}
} // namespace cutum
