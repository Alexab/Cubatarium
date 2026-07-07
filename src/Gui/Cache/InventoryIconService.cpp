#include "Gui/Cache/InventoryIconService.h"

#include "App/Platform/IUPlatformPaths.h"
#include "Render/GlIncludes.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace
{
constexpr int kManifestVersion = 2;
}

UInventoryIconService::UInventoryIconService() = default;

bool UInventoryIconService::Initialize()
{
  if (auto *paths = IUPlatformPaths::TryGet())
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
  unsigned char *data =
      stbi_load(path.string().c_str(), &width, &height, &channels, 4);
  if (!data || width <= 0 || height <= 0 || width != height)
  {
    stbi_image_free(data);
    ++Metrics.PngReadFailures;
    return false;
  }
  if (expectedSize > 0 && width != expectedSize)
  {
    stbi_image_free(data);
    ++Metrics.PngReadFailures;
    return false;
  }
  outSize = width;
  const size_t bytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  pixels.resize(bytes);
  std::memcpy(pixels.data(), data, bytes);
  stbi_image_free(data);
  return true;
}

bool UInventoryIconService::WritePngRgba(const std::filesystem::path &path,
                                         int size,
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
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return false;
  }
  pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  return true;
}

GLuint UInventoryIconService::CreateTextureFromPixels(
    int size, const std::vector<unsigned char> &pixels) const
{
  if (size <= 0 ||
      pixels.size() !=
          static_cast<size_t>(size) * static_cast<size_t>(size) * 4)
  {
    return 0;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
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
  if (!SaveManifest())
  {
    return false;
  }
  ++Metrics.RenderStores;
  return true;
}

void UInventoryIconService::InvalidateAll()
{
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

} // namespace cutum
