#include "ResourcePacks/PlaceholderTextureCache.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
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

UPlaceholderTextureCache::UPlaceholderTextureCache(std::filesystem::path cacheDir,
                                                   int tileSize,
                                                   glm::vec3 background)
    : CacheDir(std::move(cacheDir)), Background(background),
      DefaultTileSize(tileSize)
{
  std::error_code ec;
  std::filesystem::create_directories(CacheDir, ec);
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
    return it->second;
  }
  const std::string stem = MakeStem(key);
  KeyToStem[key] = stem;
  StemPixels[stem] = Rasterize(blockName, faceIndex, tileSize);
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
