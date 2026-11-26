#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace cutum
{

enum class VisualLoaderKind { Gltf, Skeleton, Rigid, Item, Count };

struct VisualLoaderSourceRow
{
  size_t attempts{}, completed{}, active{}, peakActive{}, documents{}, binaryReads{};
  size_t unclosedDocuments{}, unsupportedSamples{};
  size_t jsonBytesMax{}, jsonHeapUpperMax{}, binaryBytesMax{}, binaryBackingMax{};
  size_t scratchBackingMax{}, pathUnitsMax{};
};

using VisualLoaderSourceSnapshot =
    std::array<VisualLoaderSourceRow, static_cast<size_t>(VisualLoaderKind::Count)>;

namespace visual_loader_detail
{
inline std::mutex Mutex;
inline VisualLoaderSourceSnapshot Sources;
}

inline VisualLoaderSourceSnapshot GetVisualLoaderSources()
{
  const std::lock_guard lock(visual_loader_detail::Mutex);
  return visual_loader_detail::Sources;
}

// Per cold file load, with independent maxima for source components. JSON's
// envelope is the parser's ordinary-new bound. Binary backing and selected
// accessor arrays report requested backing. Cache tables, filesystem/CRT
// internals, renderer data, native stack and process high-water are separate.
class VisualLoaderSource
{
public:
  explicit VisualLoaderSource(VisualLoaderKind kind)
      : Kind(static_cast<size_t>(kind))
  {
    const std::lock_guard lock(visual_loader_detail::Mutex);
    auto &row = visual_loader_detail::Sources[Kind];
    ++row.attempts;
    row.peakActive = std::max(row.peakActive, ++row.active);
  }

  VisualLoaderSource(const VisualLoaderSource &) = delete;
  VisualLoaderSource &operator=(const VisualLoaderSource &) = delete;

  ~VisualLoaderSource()
  {
    const std::lock_guard lock(visual_loader_detail::Mutex);
    auto &row = visual_loader_detail::Sources[Kind];
    --row.active;
    row.completed += Complete;
    row.documents += Documents;
    row.binaryReads += BinaryReads;
    row.unclosedDocuments += Unclosed;
    row.unsupportedSamples += !Binary.bytes.supported || !Supported;
    row.jsonBytesMax = std::max(row.jsonBytesMax, JsonBytes);
    row.jsonHeapUpperMax = std::max(row.jsonHeapUpperMax, JsonUpper);
    row.binaryBytesMax = std::max(row.binaryBytesMax, BinaryBytes);
    row.binaryBackingMax = std::max(row.binaryBackingMax, Binary.bytes.requested);
    row.scratchBackingMax = std::max(row.scratchBackingMax, ScratchBytes);
    row.pathUnitsMax = std::max(row.pathUnitsMax, PathUnits);
  }

  void Success() noexcept { Complete = true; }
  void Path(const std::filesystem::path &path) noexcept
  {
    PathUnits = std::max(PathUnits, path.native().size());
  }

  CatalogJsonDocument ReadDocument(std::ifstream &file, CatalogJsonKind kind,
                                   bool rejectTrailing = true)
  {
    try
    {
      auto data = ParseAndCloseCatalogFileJson(file, kind, rejectTrailing);
      RecordDocument(file);
      return data;
    }
    catch (...)
    {
      RecordDocument(file);
      throw;
    }
  }

  template<class T> void BinaryTable(const std::vector<T> &buffers) noexcept
  {
    Binary.Buffer(buffers.capacity(), sizeof(T));
  }

  void BinaryBuffer(const std::vector<uint8_t> &buffer) noexcept
  {
    ++BinaryReads;
    Binary.Buffer(buffer.capacity(), sizeof(uint8_t));
    bool overflow = false;
    detail::JsonSourceAdd(BinaryBytes, buffer.size(), overflow);
    Supported &= !overflow;
  }

  template<class... T> void Scratch(const std::vector<T> &... arrays) noexcept
  {
    StlSourceStorage live;
    (live.Buffer(arrays.capacity(), sizeof(T)), ...);
    ScratchBytes = std::max(ScratchBytes, live.bytes.requested);
    Supported &= live.bytes.supported;
  }

private:
  void RecordDocument(const std::ifstream &file) noexcept
  {
    ++Documents;
    Unclosed += file.is_open();
    const auto shape = GetLastCatalogJsonSource();
    Supported &= shape.model_supported && !shape.overflow;
    JsonBytes = shape.bytes;
    JsonUpper = shape.requested_heap_upper;
  }

  const size_t Kind;
  StlSourceStorage Binary;
  size_t Documents{}, BinaryReads{}, Unclosed{}, JsonBytes{}, JsonUpper{};
  size_t BinaryBytes{}, ScratchBytes{}, PathUnits{};
  bool Complete{}, Supported{true};
};

// Read exactly one checked regular-file extent into one allocation. The
// stream leaves scope before the caller starts converting the asset.
inline std::optional<std::vector<uint8_t>> ReadVisualModelBuffer(
    const std::filesystem::path &path, VisualLoaderSource &source,
    std::optional<size_t> expected = {})
{
  source.Path(path);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return {};
  const auto end = file.tellg();
  if (end < 0)
    return {};
  const auto length = static_cast<std::uintmax_t>(static_cast<std::streamoff>(end));
  std::vector<uint8_t> bytes;
  if (length > bytes.max_size() ||
      length > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()) ||
      (expected && length != *expected))
    return {};
  if (!file.seekg(0))
    return {};
  bytes.resize(static_cast<size_t>(length));
  // Record the current request even if the file changes during this read.
  source.BinaryBuffer(bytes);
  if ((!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size()))) ||
      file.peek() != std::char_traits<char>::eof() || file.bad())
    return {};
  return std::optional{std::move(bytes)};
}

} // namespace cutum
