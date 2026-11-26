#include "Gui/Cache/InventoryIconService.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Render/GlIncludes.h"
#include "Render/Textures/GlTextureOwner.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <cstring>
#include <deque>
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
  std::deque<Pending> Jobs;
};

UInventoryIconService::UInventoryIconService()
    : Readbacks(std::make_unique<ReadbackState>())
{
}

UInventoryIconService::~UInventoryIconService() = default;

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

void UInventoryIconService::DrainReadbacks()
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
    if (!pixels.empty() && WritePngRgba(CacheRoot / job.File, job.Size, pixels))
    {
      Entries[job.Key] = Entry{job.Fingerprint, job.File, job.Size};
      ManifestDirty = true;
      ++Metrics.RenderStores;
    }
    else if (pixels.empty())
      ++Metrics.PngWriteFailures;
    jobs.pop_front();
  }
}

bool UInventoryIconService::Initialize(const std::filesystem::path &cacheRoot)
{
  if (!cacheRoot.empty())
  {
    CacheRoot = cacheRoot;
  }
  else if (auto *paths = IUPlatformPaths::TryGet())
  {
    CacheRoot = paths->ResolveWritable("cache/icons");
  }
  else
  {
    CacheRoot = std::filesystem::current_path() / "cache" / "icons";
  }
  ManifestPath = CacheRoot / "manifest.json";
  std::error_code ec;
  std::filesystem::create_directories(CacheRoot, ec);
  return LoadManifest();
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

bool UInventoryIconService::LoadManifest()
{
  Entries.clear();
  if (!std::filesystem::exists(ManifestPath))
  {
    return true;
  }
  std::ifstream in(ManifestPath, std::ios::binary);
  if (!in)
  {
    return false;
  }
  nlohmann::json doc;
  try
  {
    in >> doc;
  }
  catch (...)
  {
    return false;
  }
  if (!doc.contains("entries") || !doc["entries"].is_object())
  {
    return true;
  }
  const int version = doc.value("version", 1);
  if (version != kManifestVersion)
  {
    ++Metrics.ManifestVersionMismatches;
    return true;
  }
  for (auto it = doc["entries"].begin(); it != doc["entries"].end(); ++it)
  {
    const auto &entry = it.value();
    if (!entry.is_object())
    {
      continue;
    }
    Entry data;
    data.Fingerprint = entry.value("fingerprint", "");
    data.File = entry.value("file", "");
    data.Size = entry.value("size", 0);
    if (!data.Fingerprint.empty() && !data.File.empty() && data.Size > 0)
    {
      Entries[it.key()] = data;
    }
  }
  return true;
}

bool UInventoryIconService::SaveManifest() const
{
  nlohmann::json doc;
  doc["version"] = kManifestVersion;
  nlohmann::json entries = nlohmann::json::object();
  for (const auto &[key, entry] : Entries)
  {
    entries[key] = {{"fingerprint", entry.Fingerprint},
                    {"file", entry.File},
                    {"size", entry.Size}};
  }
  doc["entries"] = entries;
  std::ofstream out(ManifestPath, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return false;
  }
  out << doc.dump(2);
  return true;
}

bool UInventoryIconService::ReadPngRgba(const std::filesystem::path &path,
                                        int expectedSize,
                                        std::vector<unsigned char> &pixels,
                                        int &outSize) const
{
  int width = 0;
  int height = 0;
  int channels = 0;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(
      stbi_load(path.string().c_str(), &width, &height, &channels, 4), stbi_image_free);
  if (!data || width <= 0 || height <= 0 || width != height)
  {
    ++Metrics.PngReadFailures;
    return false;
  }
  if (expectedSize > 0 && width != expectedSize)
  {
    ++Metrics.PngReadFailures;
    return false;
  }
  outSize = width;
  const size_t bytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  pixels.resize(bytes);
  std::memcpy(pixels.data(), data.get(), bytes);
  return true;
}

bool UInventoryIconService::WritePngRgba(
    const std::filesystem::path &path, int size,
    const std::vector<unsigned char> &pixels) const
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
  const bool ok = stbi_write_png(path.string().c_str(), size, size, 4,
                                 pixels.data(), size * 4) != 0;
  if (!ok)
  {
    ++Metrics.PngWriteFailures;
  }
  return ok;
}

bool UInventoryIconService::CaptureTexturePixels(
    GLuint texture, int size, std::vector<unsigned char> &pixels) const
{
  if (texture == 0 || size <= 0)
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
  return true;
}

GLuint UInventoryIconService::CreateTextureFromPixels(
    int size, const std::vector<unsigned char> &pixels) const
{
  if (size <= 0 || pixels.size() != static_cast<size_t>(size) *
                                        static_cast<size_t>(size) * 4)
  {
    return 0;
  }
  GLint previousTexture{};
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex)
    return 0;
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, GLuint(previousTexture));
  return tex;
}

bool UInventoryIconService::TryLoadIconTexture(const std::string &kind,
                                               const std::string &entityId,
                                               const std::string &variantId,
                                               const std::string &fingerprint,
                                               int expectedSize,
                                               GLuint &outTexture)
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
  std::vector<unsigned char> pixels;
  const std::filesystem::path filePath = CacheRoot / it->second.File;
  int pngSize = 0;
  if (!ReadPngRgba(filePath, expectedSize, pixels, pngSize))
  {
    ++Metrics.CacheMisses;
    return false;
  }
  outTexture = CreateTextureFromPixels(pngSize, pixels);
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
                                             int size, GLuint texture)
{
  if (texture == 0 || size <= 0 || fingerprint.empty() || entityId.empty() ||
      kind.empty())
  {
    return false;
  }
  if (kind == "item")
  {
    // Bounded optional cache work: the visible in-memory icon is unaffected if
    // persistence cannot be queued. No gameplay resources are held here.
    auto &jobs = Readbacks->Jobs;
    if (jobs.size() >= kMaxReadbacks || size > kMaxReadbackSize)
      return false;
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
  if (!WritePngRgba(path, size, pixels))
  {
    return false;
  }
  Entries[key] = Entry{fingerprint, fileName, size};
  // UX R2: O(n) manifest rewrites per icon batch made icon generation
  // quadratic; serialize once per FlushManifest instead.
  ManifestDirty = true;
  ++Metrics.RenderStores;
  return true;
}

void UInventoryIconService::FlushManifest()
{
  DrainReadbacks();
  if (!ManifestDirty || !Readbacks->Jobs.empty())
  {
    return;
  }
  if (SaveManifest())
    ManifestDirty = false;
}

void UInventoryIconService::InvalidateAll()
{
  CancelPendingItemReadbacks();
  ManifestDirty = false;
  Entries.clear();
  std::error_code ec;
  if (std::filesystem::exists(CacheRoot))
  {
    for (const auto &entry : std::filesystem::directory_iterator(CacheRoot))
    {
      if (!entry.is_regular_file())
      {
        continue;
      }
      const std::filesystem::path path = entry.path();
      if (path.extension() == ".png" || path.filename() == "manifest.json")
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
  std::vector<std::string> eraseKeys;
  for (const auto &[key, entry] : Entries)
  {
    if (key.rfind(kind + ":", 0) == 0)
    {
      std::error_code ec;
      std::filesystem::remove(CacheRoot / entry.File, ec);
      eraseKeys.push_back(key);
    }
  }
  for (const std::string &key : eraseKeys)
  {
    Entries.erase(key);
  }
  SaveManifest();
}

} // namespace cutum
