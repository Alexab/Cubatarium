#include "ResourcePacks/PlaceholderTextureCache.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

namespace cutum
{

namespace
{

glm::vec3 ParseHexColor(const std::string &hex, glm::vec3 fallback)
{
  if (hex.size() != 7 || hex[0] != '#')
  {
    return fallback;
  }
  auto hexByte = [&](size_t i) -> int {
    const char c = hex[i];
    if (c >= '0' && c <= '9')
    {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
      return 10 + (c - 'A');
    }
    return 0;
  };
  const float r = (hexByte(1) * 16 + hexByte(2)) / 255.0f;
  const float g = (hexByte(3) * 16 + hexByte(4)) / 255.0f;
  const float b = (hexByte(5) * 16 + hexByte(6)) / 255.0f;
  return glm::vec3(r, g, b);
}

} // namespace

namespace
{

std::filesystem::path PlaceholderFilePath(const std::filesystem::path &cacheDir,
                                          const std::string &stem)
{
  return cacheDir / (stem + ".rgba");
}

bool SavePlaceholderFile(const std::filesystem::path &path,
                         const TexturePixelData &pixels)
{
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open())
  {
    return false;
  }
  const uint32_t width = static_cast<uint32_t>(pixels.Width);
  const uint32_t height = static_cast<uint32_t>(pixels.Height);
  const uint32_t size = static_cast<uint32_t>(pixels.Rgba.size());
  out.write(reinterpret_cast<const char *>(&width), sizeof(width));
  out.write(reinterpret_cast<const char *>(&height), sizeof(height));
  out.write(reinterpret_cast<const char *>(&size), sizeof(size));
  if (size > 0)
  {
    out.write(reinterpret_cast<const char *>(pixels.Rgba.data()), size);
  }
  return out.good();
}

bool LoadPlaceholderFile(const std::filesystem::path &path, TexturePixelData &out)
{
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
  {
    return false;
  }
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t size = 0;
  in.read(reinterpret_cast<char *>(&width), sizeof(width));
  in.read(reinterpret_cast<char *>(&height), sizeof(height));
  in.read(reinterpret_cast<char *>(&size), sizeof(size));
  if (!in.good())
  {
    return false;
  }
  std::vector<uint8_t> rgba(size);
  if (size > 0)
  {
    in.read(reinterpret_cast<char *>(rgba.data()), size);
    if (!in.good())
    {
      return false;
    }
  }
  out.Width = static_cast<int>(width);
  out.Height = static_cast<int>(height);
  out.Rgba = std::move(rgba);
  return true;
}

} // namespace

UPlaceholderTextureCache::UPlaceholderTextureCache(std::filesystem::path cacheDir,
                                                   int tileSize,
                                                   glm::vec3 background,
                                                   size_t maxEntries)
    : CacheDir(std::move(cacheDir)), Background(background),
      DefaultTileSize(tileSize), MaxEntries(std::max<size_t>(1, maxEntries))
{
  std::error_code ec;
  std::filesystem::create_directories(CacheDir, ec);
}

void UPlaceholderTextureCache::TouchLru(const std::string &key)
{
  const auto it = LruIt.find(key);
  if (it != LruIt.end())
  {
    LruOrder.erase(it->second);
  }
  LruOrder.push_front(key);
  LruIt[key] = LruOrder.begin();
}

void UPlaceholderTextureCache::EvictIfNeeded()
{
  while (KeyToStem.size() > MaxEntries && !LruOrder.empty())
  {
    const std::string victim = LruOrder.back();
    LruOrder.pop_back();
    LruIt.erase(victim);
    const auto stemIt = KeyToStem.find(victim);
    if (stemIt != KeyToStem.end())
    {
      std::error_code ec;
      std::filesystem::remove(PlaceholderFilePath(CacheDir, stemIt->second), ec);
      StemPixels.erase(stemIt->second);
      KeyToStem.erase(stemIt);
    }
  }
}

std::string UPlaceholderTextureCache::MakeKey(const std::string &blockName,
                                              int faceIndex, int tileSize) const
{
  return blockName + "|" + std::to_string(faceIndex) + "|" +
         std::to_string(tileSize);
}

std::string
UPlaceholderTextureCache::MakeStem(const std::string &key) const
{
  const size_t h = std::hash<std::string>{}(key);
  std::ostringstream oss;
  oss << "__ph_" << std::hex << std::setw(8) << std::setfill('0')
      << static_cast<uint32_t>(h);
  return oss.str();
}

TexturePixelData UPlaceholderTextureCache::Rasterize(const std::string &blockName,
                                                     int faceIndex,
                                                     int tileSize) const
{
  TexturePixelData out;
  out.Width = tileSize;
  out.Height = tileSize;
  out.Rgba.resize(static_cast<size_t>(tileSize * tileSize * 4));
  const uint8_t br = static_cast<uint8_t>(Background.r * 255.0f);
  const uint8_t bg = static_cast<uint8_t>(Background.g * 255.0f);
  const uint8_t bb = static_cast<uint8_t>(Background.b * 255.0f);
  for (size_t i = 0; i < out.Rgba.size(); i += 4)
  {
    out.Rgba[i] = br;
    out.Rgba[i + 1] = bg;
    out.Rgba[i + 2] = bb;
    out.Rgba[i + 3] = 255;
  }

  std::string label = blockName;
  if (label.size() > 8)
  {
    label = label.substr(0, 8);
  }
  if (faceIndex >= 0)
  {
    label.push_back(static_cast<char>('0' + (faceIndex % 10)));
  }

  const int glyphW = std::max(1, tileSize / static_cast<int>(label.size() + 1));
  for (size_t ci = 0; ci < label.size(); ++ci)
  {
    const unsigned char ch = static_cast<unsigned char>(label[ci]);
    const int col = static_cast<int>(ci) * (glyphW + 1) + 1;
    for (int y = 2; y < tileSize - 2; ++y)
    {
      for (int x = col; x < col + glyphW && x < tileSize - 1; ++x)
      {
        const int bit = (ch + y + x) & 3;
        if (bit == 0)
        {
          const size_t idx = static_cast<size_t>((y * tileSize + x) * 4);
          out.Rgba[idx] = 255;
          out.Rgba[idx + 1] = 255;
          out.Rgba[idx + 2] = 255;
        }
      }
    }
  }
  return out;
}

std::string UPlaceholderTextureCache::GetOrCreateStem(const std::string &blockName,
                                                      int faceIndex,
                                                      int tileSize)
{
  const std::string key = MakeKey(blockName, faceIndex, tileSize);
  const auto it = KeyToStem.find(key);
  if (it != KeyToStem.end())
  {
    TouchLru(key);
    return it->second;
  }
  const std::string stem = MakeStem(key);
  KeyToStem[key] = stem;
  TexturePixelData pixels;
  if (!LoadPlaceholderFile(PlaceholderFilePath(CacheDir, stem), pixels))
  {
    pixels = Rasterize(blockName, faceIndex, tileSize);
    SavePlaceholderFile(PlaceholderFilePath(CacheDir, stem), pixels);
  }
  StemPixels[stem] = std::move(pixels);
  TouchLru(key);
  EvictIfNeeded();
  return stem;
}

void UPlaceholderTextureCache::RegisterAll(UTextureBaseStorage &storage) const
{
  for (const auto &entry : StemPixels)
  {
    storage.RegisterPixels(entry.first, entry.second);
  }
}

} // namespace cutum
