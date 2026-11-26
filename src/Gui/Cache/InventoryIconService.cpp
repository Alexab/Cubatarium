#include "Gui/Cache/InventoryIconService.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Core/Memory/StbiAllocation.h"
#include "Gui/Cache/InventoryIconPng.h"
#include "Render/GlIncludes.h"
#include "Render/Engine/TerrainGpuBuffers.h"
#include "Render/Textures/GlTextureOwner.h"
#include "ThirdParty/stb_image.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <list>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace
{
constexpr int kManifestVersion = 2;
constexpr size_t kMaxReadbacks = 256;
constexpr int kMaxReadbackSize = 512;
constexpr size_t kPackBytesUpper = kMaxReadbacks * kMaxReadbackSize * kMaxReadbackSize * 4;

bool TextureMatchesIconExtent(GLuint texture, int size) noexcept
{
  if (!glIsTexture(texture))
    return false;
  GLint previous{}, binding{}, width{}, height{};
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
  if (static_cast<GLuint>(binding) == texture)
  {
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  }
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
  return glGetError() == GL_NO_ERROR && width == size && height == size;
}

template<class Table> NameStorageEnvelope ManifestTableStorage(const Table &entries) noexcept
{
  StlSourceStorage table;
  table.Hash(entries);
  for (const auto &[key, entry] : entries)
  {
    table.Text(key);
    table.Text(entry.Fingerprint);
    table.Text(entry.File);
  }
  return table.bytes;
}

bool PublishIconFile(const std::filesystem::path &path, const char *bytes, size_t length,
                     const InventoryIconIoProbe *manifestProbe = nullptr,
                     const std::string *manifestPayload = nullptr)
{
  auto temporary = path;
  temporary += ".tmp";
  if (manifestProbe)
  {
    StlSourceStorage storage;
    storage.Text(temporary.native());
    storage.Text(*manifestPayload);
    manifestProbe->sample(manifestProbe->context, {InventoryIconIoPhase::ManifestFile,
        storage.bytes.requested, 0, 0, length, storage.bytes.supported});
  }
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out.write(bytes, static_cast<std::streamsize>(length));
  out.close();
  std::error_code ec;
  if (out)
  {
    std::filesystem::rename(temporary, path, ec);
    if (!ec)
      return true;
  }
  std::filesystem::remove(temporary, ec);
  return false;
}

std::FILE *OpenIconPng(const std::filesystem::path &path)
{
#ifdef _WIN32
  return _wfopen(path.c_str(), L"rb");
#else
  return std::fopen(path.c_str(), "rb");
#endif
}
}

// Pixel-pack state belongs to the caller, including distinct read/draw targets.
namespace
{
struct IconReadbackScope
{
  GLint ReadFbo{}, PackBuffer{}, Alignment{}, RowLength{}, SkipRows{},
      SkipPixels{};
  IconReadbackScope()
  {
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &ReadFbo);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &PackBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &RowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &SkipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &SkipPixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  }
  ~IconReadbackScope()
  {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, PackBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, Alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, RowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, SkipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, SkipPixels);
  }
};

struct IconUnpackScope
{
  static constexpr std::array<GLenum, 4> Parameters{
      GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS};
  std::array<GLint, Parameters.size()> Values{};
  GLint UnpackBuffer{}, Texture{};

  IconUnpackScope()
  {
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &UnpackBuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &Texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (size_t i = 0; i < Parameters.size(); ++i)
    {
      glGetIntegerv(Parameters[i], &Values[i]);
      glPixelStorei(Parameters[i], i == 0 ? 1 : 0);
    }
  }
  ~IconUnpackScope()
  {
    for (size_t i = 0; i < Parameters.size(); ++i)
      glPixelStorei(Parameters[i], Values[i]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(UnpackBuffer));
    glBindTexture(GL_TEXTURE_2D, GLuint(Texture));
  }
};
} // namespace

struct UInventoryIconService::ReadbackState
{
  struct Pending
  {
    std::string Key, Fingerprint, File;
    int Size{};
    GLuint Buffer{};
    GLsync Fence{};
    Pending(std::string key, std::string fingerprint, std::string file, int size)
        : Key(std::move(key)), Fingerprint(std::move(fingerprint)), File(std::move(file)), Size(size) {}
    Pending(const Pending &) = delete;
    Pending &operator=(const Pending &) = delete;
    ~Pending()
    {
      if (Fence)
        glDeleteSync(Fence);
      if (Buffer)
        glDeleteBuffers(1, &Buffer);
    }
  };
  std::list<Pending> Jobs;
};

UInventoryIconService::UInventoryIconService()
    : Readbacks(std::make_unique<ReadbackState>())
{
}

UInventoryIconService::~UInventoryIconService() = default;

std::shared_ptr<UInventoryIconService> UInventoryIconService::Create()
{
  auto service = std::make_shared<UInventoryIconService>();
  service->FactoryOwner = true;
  return service;
}

InventoryIconStorage UInventoryIconService::InspectOwnedStorage() const noexcept
{
  InventoryIconStorage out;
  out.present = true;
  out.initialized = !CacheRoot.empty() && !ManifestPath.empty();
  out.dirty = ManifestDirty;
  out.factoryOwner = FactoryOwner;
  out.serviceBytes = sizeof(*this);
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  if (FactoryOwner)
    out.serviceRequest = sizeof(std::_Ref_count_obj2<UInventoryIconService>);
#endif
  out.readbackStateBytes = sizeof(ReadbackState);
  StlSourceStorage paths, jobs, total;
  paths.Text(CacheRoot.native());
  paths.Text(ManifestPath.native());
  const auto table = ManifestTableStorage(Entries);
  jobs.List(Readbacks->Jobs);
  for (const auto &job : Readbacks->Jobs)
  {
    jobs.Text(job.Key);
    jobs.Text(job.Fingerprint);
    jobs.Text(job.File);
  }
  out.pathBytes = paths.bytes.requested;
  out.manifestBytes = table.requested;
  out.jobBytes = jobs.bytes.requested;
  out.entries = Entries.size();
  const auto readback = InspectReadbackStorage();
  out.jobs = readback.jobs;
  out.packBufferBytes = readback.buffer_bytes;
  out.jobLimit = kMaxReadbacks;
  out.readbackSideLimit = kMaxReadbackSize;
  out.packBufferBytesUpper = kPackBytesUpper;
  for (auto bytes : {out.serviceRequest, out.readbackStateBytes, out.pathBytes, out.manifestBytes, out.jobBytes})
    total.Add(bytes);
  out.cpuBytes = total.bytes.requested;
  out.bindingsMatch = CacheRoot.empty() == ManifestPath.empty();
  out.supported = out.bindingsMatch && readback.supported && total.bytes.supported &&
                  paths.bytes.supported && table.supported && jobs.bytes.supported;
  return out;
}

void UInventoryIconService::InspectReadbackGpuBuffers(TerrainGpuBufferCensus &out) const
{
  for (const auto &job : Readbacks->Jobs)
    out.Add(TerrainGpuBufferRole::InventoryIconReadback, job.Buffer);
}

void UInventoryIconService::CancelPendingItemReadbacks() noexcept
{
  Readbacks->Jobs.clear();
}

UInventoryIconService::ReadbackStorage UInventoryIconService::InspectReadbackStorage() const noexcept
{
  ReadbackStorage out;
  const auto &jobs = Readbacks->Jobs;
  out.jobs = jobs.size();
  out.supported = jobs.size() <= kMaxReadbacks;
  if (!jobs.empty())
    out.first_buffer = jobs.front().Buffer;
  for (const auto &job : jobs)
  {
    if (!job.Buffer || !job.Fence || job.Size <= 0 || job.Size > kMaxReadbackSize)
    {
      out.supported = false;
      continue;
    }
    out.buffer_bytes += size_t(job.Size) * job.Size * 4;
  }
  return out;
}

void UInventoryIconService::DrainReadbacks(const InventoryIconIoProbe *probe)
{
  auto &jobs = Readbacks->Jobs;
  // Optional disk caching must never wait for unfinished GPU work.
  for (int completed = 0; completed < 2 && !jobs.empty(); ++completed)
  {
    auto &job = jobs.front();
    const GLenum ready = glClientWaitSync(job.Fence, 0, 0);
    if (ready == GL_TIMEOUT_EXPIRED)
      break;
    const size_t bytes = size_t(job.Size) * job.Size * 4;
    std::vector<unsigned char> pixels;
    if (ready != GL_WAIT_FAILED)
    {
      // Allocate CPU storage before mapping the immutable pack buffer.
      pixels.resize(bytes);
      IconReadbackScope state;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, job.Buffer);
      const void *mapped =
          glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
      if (mapped)
      {
        std::memcpy(pixels.data(), mapped, bytes);
        if (!glUnmapBuffer(GL_PIXEL_PACK_BUFFER))
          pixels.clear();
      }
      else
        pixels.clear();
    }
    if (!pixels.empty() && WritePngRgba(CacheRoot / job.File, job.Size, pixels, probe))
    {
      Entries.insert_or_assign(job.Key, Entry{job.Fingerprint, job.File, job.Size});
      ManifestDirty = true;
      ++Metrics.RenderStores;
    }
    else if (pixels.empty())
      ++Metrics.PngWriteFailures;
    jobs.pop_front();
  }
}

bool UInventoryIconService::Initialize(const std::filesystem::path &cacheRoot, const InventoryIconIoProbe *probe)
{
  auto root = cacheRoot;
  if (root.empty())
  {
    if (auto *paths = IUPlatformPaths::TryGet())
      root = paths->ResolveWritable("cache/icons");
    else
      root = std::filesystem::current_path() / "cache" / "icons";
  }
  auto manifest = root / "manifest.json";
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec)
    return false;
  const bool sameRoot = manifest == ManifestPath;
  if (ManifestDirty && sameRoot && !SaveManifest(probe))
    return false;

  ManifestEntries entries;
  bool versionMismatch = false;
  if (!LoadManifest(manifest, entries, versionMismatch, probe) ||
      (ManifestDirty && !sameRoot && !SaveManifest(probe)))
    return false;

  CancelPendingItemReadbacks();
  CacheRoot.swap(root);
  ManifestPath.swap(manifest);
  Entries.swap(entries);
  ManifestDirty = false;
  Metrics.ManifestVersionMismatches += versionMismatch;
  return true;
}

std::string UInventoryIconService::BuildCacheKey(const std::string &kind,
                                                 const std::string &entityId,
                                                 const std::string &variantId)
{
  if (variantId.empty())
  {
    return kind + ":" + entityId;
  }
  return kind + ":" + entityId + ":" + variantId;
}

std::string UInventoryIconService::SanitizeFileToken(const std::string &raw)
{
  std::string out;
  out.reserve(raw.size());
  for (char c : raw)
  {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')
    {
      out.push_back(c);
    }
    else
    {
      out.push_back('_');
    }
  }
  return out;
}

std::string UInventoryIconService::HashFingerprint(const std::string &value)
{
  constexpr uint64_t kOffset = 1469598103934665603ull;
  constexpr uint64_t kPrime = 1099511628211ull;
  uint64_t hash = kOffset;
  for (unsigned char c : value)
  {
    hash ^= static_cast<uint64_t>(c);
    hash *= kPrime;
  }
  std::ostringstream oss;
  oss << std::hex << hash;
  return oss.str();
}

bool UInventoryIconService::LoadManifest(const std::filesystem::path &path,
                                         ManifestEntries &entries, bool &versionMismatch,
                                         const InventoryIconIoProbe *probe)
{
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
  {
    return !ec;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return false;
  }
  try
  {
    const auto doc = ParseAndCloseCatalogFileJson(in, CatalogJsonKind::Manifest);
    if (!doc.contains("entries") || !doc["entries"].is_object())
      return true;
    if (doc.value("version", 1) != kManifestVersion)
    {
      versionMismatch = true;
      return true;
    }
    for (const auto &[key, entry] : doc["entries"].items())
    {
      if (!entry.is_object())
        continue;
      Entry data;
      data.Fingerprint = entry.value("fingerprint", "");
      data.File = entry.value("file", "");
      data.Size = entry.value("size", 0);
      if (!data.Fingerprint.empty() && !data.File.empty() && data.Size > 0)
      {
        const std::filesystem::path file(data.File);
        if (!file.has_root_path() && file == file.filename() && file.extension() == ".png")
          entries.emplace(key, std::move(data));
      }
    }
    if (probe)
    {
      const auto parser = GetLastCatalogJsonSource();
      const auto table = ManifestTableStorage(entries);
      StlSourceStorage storage;
      storage.JsonTree(static_cast<const nlohmann::json &>(doc));
      storage.Add(doc.RetirementRequestedBytes());
      storage.Add(table.requested);
      storage.Text(path.native());
      probe->sample(probe->context, {InventoryIconIoPhase::ManifestRead, storage.bytes.requested,
          0, parser.requested_heap_upper, parser.bytes,
          storage.bytes.supported && table.supported && parser.model_supported && !parser.overflow});
    }
  }
  catch (const nlohmann::json::exception &)
  {
    return false;
  }
  return true;
}

bool UInventoryIconService::SaveManifest(const InventoryIconIoProbe *probe) const
{
  if (ManifestPath.empty())
    return false;
  std::string payload;
  {
    nlohmann::json doc;
    doc["version"] = kManifestVersion;
    auto &entries = doc["entries"] = nlohmann::json::object();
    for (const auto &[key, entry] : Entries)
      entries[key] = {{"fingerprint", entry.Fingerprint}, {"file", entry.File}, {"size", entry.Size}};
    payload = doc.dump(2);
    if (probe)
    {
      StlSourceStorage storage;
      storage.JsonTree(doc);
      storage.Text(payload);
      probe->sample(probe->context, {InventoryIconIoPhase::ManifestWrite,
          storage.bytes.requested, 0, 0, payload.size(), storage.bytes.supported});
    }
  }
  return PublishIconFile(ManifestPath, payload.data(), payload.size(), probe, &payload);
}

bool UInventoryIconService::WritePngRgba(
    const std::filesystem::path &path, int size,
    const std::vector<unsigned char> &pixels, const InventoryIconIoProbe *probe) const
{
  if (size <= 0)
  {
    ++Metrics.PngWriteFailures;
    return false;
  }
  const size_t expected =
      static_cast<size_t>(size) * static_cast<size_t>(size) * 4;
  if (pixels.size() != expected)
  {
    ++Metrics.PngWriteFailures;
    return false;
  }
  struct PngWriteContext
  {
    const std::filesystem::path &path;
    const std::vector<unsigned char> &pixels;
    const InventoryIconIoProbe *probe;
  } context{path, pixels, probe};
  const bool ok = EncodeInventoryIconPng(pixels.data(), size, &context,
      [](void *value, const unsigned char *bytes, size_t length, size_t codecBytes)
      {
        const auto &sink = *static_cast<const PngWriteContext *>(value);
        if (sink.probe)
        {
          StlSourceStorage storage;
          storage.Text(sink.path.native());
          storage.Buffer(sink.pixels.capacity(), sizeof(unsigned char));
          sink.probe->sample(sink.probe->context, {InventoryIconIoPhase::PngEncode,
              storage.bytes.requested, codecBytes, 0, length, storage.bytes.supported});
        }
        return PublishIconFile(sink.path, reinterpret_cast<const char *>(bytes), length);
      });
  if (!ok)
  {
    ++Metrics.PngWriteFailures;
  }
  return ok;
}

bool UInventoryIconService::CaptureTexturePixels(
    GLuint texture, int size, std::vector<unsigned char> &pixels) const
{
  if (texture == 0 || size <= 0 ||
      static_cast<size_t>(size) > pixels.max_size() / 4 / static_cast<size_t>(size))
  {
    return false;
  }
  pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  IconReadbackScope state;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  UGlFramebufferOwner fbo;
  if (!fbo.Get())
    return false;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.Get());
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, texture, 0);
  const GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
  {
    return false;
  }
  glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  return glGetError() == GL_NO_ERROR;
}

GLuint UInventoryIconService::CreateTextureFromPixels(int size, const unsigned char *pixels) const
{
  GLint maxTextureSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  if (!pixels || size <= 0 || size > maxTextureSize)
  {
    return 0;
  }
  IconUnpackScope state;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

bool UInventoryIconService::TryLoadIconTexture(const std::string &kind,
                                               const std::string &entityId,
                                               const std::string &variantId,
                                               const std::string &fingerprint,
                                               int expectedSize,
                                               GLuint &outTexture, const InventoryIconIoProbe *probe)
{
  outTexture = 0;
  if (fingerprint.empty() || entityId.empty() || kind.empty())
  {
    ++Metrics.CacheMisses;
    return false;
  }
  const std::string key = BuildCacheKey(kind, entityId, variantId);
  const auto it = Entries.find(key);
  if (it == Entries.end() || it->second.Fingerprint != fingerprint)
  {
    ++Metrics.CacheMisses;
    return false;
  }
  const std::filesystem::path filePath = CacheRoot / it->second.File;
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(nullptr, stbi_image_free);
  int width = 0, height = 0, channels = 0;
  {
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(OpenIconPng(filePath), std::fclose);
    constexpr std::array<unsigned char, 8> pngSignature{137, 80, 78, 71, 13, 10, 26, 10};
    std::array<unsigned char, 8> signature{};
    if (file && std::fread(signature.data(), 1, signature.size(), file.get()) == signature.size() &&
        signature == pngSignature && std::fseek(file.get(), 0, SEEK_SET) == 0 &&
        stbi_info_from_file(file.get(), &width, &height, &channels) && width > 0 &&
        width == height && (expectedSize <= 0 || width == expectedSize))
      pixels.reset(stbi_load_from_file(file.get(), &width, &height, &channels, 4));
  }
  if (!pixels || width <= 0 || width != height || (expectedSize > 0 && width != expectedSize))
  {
    ++Metrics.PngReadFailures;
    ++Metrics.CacheMisses;
    return false;
  }
  if (probe)
  {
    const auto decoded = InspectStbiAllocation(pixels.get());
    StlSourceStorage storage;
    storage.Text(filePath.native());
    storage.Text(key);
    probe->sample(probe->context, {InventoryIconIoPhase::PngDecode, storage.bytes.requested,
        decoded.request_bytes, 0, decoded.payload_bytes, storage.bytes.supported});
  }
  outTexture = CreateTextureFromPixels(width, pixels.get());
  if (outTexture == 0)
  {
    ++Metrics.CacheMisses;
    return false;
  }
  ++Metrics.CacheHits;
  return true;
}

bool UInventoryIconService::StoreIconTexture(const std::string &kind,
                                             const std::string &entityId,
                                             const std::string &variantId,
                                             const std::string &fingerprint,
                                             int size, GLuint texture, const InventoryIconIoProbe *probe)
{
  if (CacheRoot.empty() || texture == 0 || size <= 0 || fingerprint.empty() || entityId.empty() ||
      kind.empty())
  {
    return false;
  }
  if (kind == "item" && (Readbacks->Jobs.size() >= kMaxReadbacks || size > kMaxReadbackSize))
    return false;
  if (!TextureMatchesIconExtent(texture, size))
    return false;
  if (kind == "item")
  {
    // Bounded optional cache work: the visible in-memory icon is unaffected if
    // persistence cannot be queued. No gameplay resources are held here.
    auto &jobs = Readbacks->Jobs;
    auto file = SanitizeFileToken(kind + "_" + entityId + "_" + variantId) + "_" +
                HashFingerprint(fingerprint) + ".png";
    auto &job = jobs.emplace_back(BuildCacheKey(kind, entityId, variantId), fingerprint,
                                  std::move(file), size);
    IconReadbackScope state;
    UGlFramebufferOwner fbo;
    if (!fbo.Get())
    {
      jobs.pop_back();
      return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.Get());
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
      jobs.pop_back();
      return false;
    }
    glGenBuffers(1, &job.Buffer);
    if (!job.Buffer)
    {
      jobs.pop_back();
      return false;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, job.Buffer);
    const int requestedBytes = size * size * 4;
    glBufferData(GL_PIXEL_PACK_BUFFER, requestedBytes, nullptr, GL_STREAM_READ);
    GLint allocatedBytes = 0;
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
    if (allocatedBytes != requestedBytes)
    {
      jobs.pop_back();
      return false;
    }
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (glGetError() != GL_NO_ERROR)
    {
      jobs.pop_back();
      return false;
    }
    job.Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!job.Fence)
    {
      jobs.pop_back();
      return false;
    }
    return true;
  }
  std::vector<unsigned char> pixels;
  if (!CaptureTexturePixels(texture, size, pixels))
  {
    return false;
  }
  const std::string key = BuildCacheKey(kind, entityId, variantId);
  const std::string safe =
      SanitizeFileToken(kind + "_" + entityId + "_" + variantId);
  const std::string fileName =
      safe + "_" + HashFingerprint(fingerprint) + ".png";
  const std::filesystem::path path = CacheRoot / fileName;
  if (!WritePngRgba(path, size, pixels, probe))
  {
    return false;
  }
  Entries.insert_or_assign(key, Entry{fingerprint, fileName, size});
  // UX R2: O(n) manifest rewrites per icon batch made icon generation
  // quadratic; serialize once per FlushManifest instead.
  ManifestDirty = true;
  ++Metrics.RenderStores;
  return true;
}

void UInventoryIconService::FlushManifest(const InventoryIconIoProbe *probe)
{
  DrainReadbacks(probe);
  if (!ManifestDirty || !Readbacks->Jobs.empty())
  {
    return;
  }
  if (SaveManifest(probe))
    ManifestDirty = false;
}

void UInventoryIconService::InvalidateAll()
{
  CancelPendingItemReadbacks();
  ManifestDirty = false;
  Entries.clear();
  std::error_code ec;
  if (!CacheRoot.empty() && std::filesystem::exists(CacheRoot))
  {
    for (const auto &entry : std::filesystem::directory_iterator(CacheRoot))
    {
      if (!entry.is_regular_file())
      {
        continue;
      }
      const std::filesystem::path path = entry.path();
      if (path.extension() == ".png" || path.filename() == "manifest.json" ||
          path.filename() == "manifest.json.tmp")
      {
        std::filesystem::remove(path, ec);
      }
    }
  }
}

void UInventoryIconService::InvalidateKind(const std::string &kind)
{
  if (kind.empty())
  {
    return;
  }
  if (kind == "item")
    CancelPendingItemReadbacks();
  for (auto entry = Entries.begin(); entry != Entries.end();)
  {
    const auto &key = entry->first;
    if (key.size() > kind.size() && key[kind.size()] == ':' && key.compare(0, kind.size(), kind) == 0)
    {
      std::error_code ec;
      std::filesystem::remove(CacheRoot / entry->second.File, ec);
      ManifestDirty = true;
      entry = Entries.erase(entry);
    }
    else
      ++entry;
  }
  if (ManifestDirty && SaveManifest())
    ManifestDirty = false;
}

} // namespace cutum
