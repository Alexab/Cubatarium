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
  size_t scratchBackingMax{}, pathUnitsMax{}, resourcePathBackingMax{};
  size_t resourceRoots{}, resourceFiles{};
};

using VisualLoaderSourceSnapshot =
    std::array<VisualLoaderSourceRow, static_cast<size_t>(VisualLoaderKind::Count)>;

struct VisualFileBufferSnapshot
{
  size_t opened{}, configured{}, fallback{}, closed{}, closeFailures{};
  size_t active{}, peakActive{}, activeBufferBytes{}, peakBufferBytes{};
};

namespace visual_loader_detail
{
inline std::mutex Mutex;
inline VisualLoaderSourceSnapshot Sources;
inline VisualFileBufferSnapshot Files;
}

inline VisualFileBufferSnapshot GetVisualFileBuffers()
{
  const std::lock_guard lock(visual_loader_detail::Mutex);
  return visual_loader_detail::Files;
}

inline VisualLoaderSourceSnapshot GetVisualLoaderSources()
{
  const std::lock_guard lock(visual_loader_detail::Mutex);
  return visual_loader_detail::Sources;
}

// Per cold file load, with independent maxima for source components. JSON's
// envelope is the parser's ordinary-new bound. Binary backing and selected
// accessor arrays report requested backing. Path samples include the live
// resource names at the containment check, after filesystem calls return.
// Cache tables, internal path/CRT requests, native stack and process high-water
// are separate.
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
    row.resourceRoots += ResourceRoots;
    row.resourceFiles += ResourceFiles;
    row.unsupportedSamples += !Binary.bytes.supported || !Supported;
    row.jsonBytesMax = std::max(row.jsonBytesMax, JsonBytes);
    row.jsonHeapUpperMax = std::max(row.jsonHeapUpperMax, JsonUpper);
    row.binaryBytesMax = std::max(row.binaryBytesMax, BinaryBytes);
    row.binaryBackingMax = std::max(row.binaryBackingMax, Binary.bytes.requested);
    row.scratchBackingMax = std::max(row.scratchBackingMax, ScratchBytes);
    row.pathUnitsMax = std::max(row.pathUnitsMax, PathUnits);
    row.resourcePathBackingMax = std::max(row.resourcePathBackingMax, ResourcePathBacking);
  }

  void Success() noexcept { Complete = true; }
  void Path(const std::filesystem::path &path) noexcept
  {
    PathUnits = std::max(PathUnits, path.native().size());
  }

  template<class... Paths>
  void ResourcePath(const std::string &uri, const Paths &... paths) noexcept
  {
    StlSourceStorage backing;
    backing.Text(uri);
    (Path(paths), ...);
    (backing.Text(paths.native()), ...);
    ResourcePathBacking = std::max(ResourcePathBacking, backing.bytes.requested);
    Supported &= backing.bytes.supported;
  }
  void ResourceRootResolved() noexcept { ++ResourceRoots; }
  void ResourceFileResolved() noexcept { ++ResourceFiles; }

  std::optional<CatalogJsonDocument> ReadDocument(const std::filesystem::path &path,
      CatalogJsonKind kind, bool rejectTrailing = true);

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
  void RecordDocument(bool closed) noexcept
  {
    ++Documents;
    Unclosed += !closed;
    const auto shape = GetLastCatalogJsonSource();
    Supported &= shape.model_supported && !shape.overflow;
    JsonBytes = shape.bytes;
    JsonUpper = shape.requested_heap_upper;
  }

  const size_t Kind;
  StlSourceStorage Binary;
  size_t Documents{}, BinaryReads{}, Unclosed{}, JsonBytes{}, JsonUpper{};
  size_t BinaryBytes{}, ScratchBytes{}, PathUnits{};
  size_t ResourceRoots{}, ResourceFiles{}, ResourcePathBacking{};
  bool Complete{}, Supported{true};
};

// The stream is closed before the owned read buffer leaves scope. On MSVC
// filebuf::setbuf must run after open and before the first seek or read.
class VisualModelInputFile
{
public:
  static constexpr size_t BufferBytes = 4096;

  VisualModelInputFile(const std::filesystem::path &path, VisualLoaderSource &source,
                       std::ios::openmode mode = std::ios::in)
      : Input(path, mode)
  {
    source.Path(path);
    if (!Input.is_open())
      return;
#ifdef _MSVC_STL_VERSION
    Configured = Input.rdbuf()->pubsetbuf(Buffer.data(),
        static_cast<std::streamsize>(Buffer.size())) == Input.rdbuf();
#endif
    const std::lock_guard lock(visual_loader_detail::Mutex);
    auto &stats = visual_loader_detail::Files;
    ++stats.opened;
    stats.configured += Configured;
    stats.fallback += !Configured;
    stats.peakActive = std::max(stats.peakActive, ++stats.active);
    stats.activeBufferBytes += Configured ? Buffer.size() : 0;
    stats.peakBufferBytes = std::max(stats.peakBufferBytes, stats.activeBufferBytes);
    Opened = true;
  }

  VisualModelInputFile(const VisualModelInputFile &) = delete;
  VisualModelInputFile &operator=(const VisualModelInputFile &) = delete;
  ~VisualModelInputFile() { Close(); }

  explicit operator bool() const { return Input.is_open(); }
  std::ifstream &Stream() noexcept { return Input; }

  bool Close() noexcept
  {
    if (!Opened)
      return true;
    try { Input.close(); } catch (...) {}
    const bool closed = !Input.is_open();
    const std::lock_guard lock(visual_loader_detail::Mutex);
    auto &stats = visual_loader_detail::Files;
    --stats.active;
    stats.activeBufferBytes -= Configured ? Buffer.size() : 0;
    stats.closed += closed;
    stats.closeFailures += !closed;
    Opened = false;
    return closed;
  }

private:
  std::array<char, BufferBytes> Buffer;
  std::ifstream Input;
  bool Opened{}, Configured{};
};

inline std::optional<CatalogJsonDocument> VisualLoaderSource::ReadDocument(
    const std::filesystem::path &path, CatalogJsonKind kind, bool rejectTrailing)
{
  VisualModelInputFile input(path, *this);
  if (!input)
    return {};
  try
  {
    auto data = ParseCatalogSourceDocument(input.Stream(), kind, rejectTrailing);
    RecordDocument(input.Close());
    return std::optional{std::move(data)};
  }
  catch (...)
  {
    RecordDocument(input.Close());
    throw;
  }
}

// Read exactly one checked regular-file extent into one allocation. The
// stream leaves scope before the caller starts converting the asset.
inline std::optional<std::vector<uint8_t>> ReadVisualModelBuffer(
    const std::filesystem::path &path, VisualLoaderSource &source,
    std::optional<size_t> expected = {})
{
  VisualModelInputFile input(path, source, std::ios::binary);
  if (!input)
    return {};
  auto &file = input.Stream();
  if (!file.seekg(0, std::ios::end))
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
