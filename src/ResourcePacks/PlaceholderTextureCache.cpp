#include "ResourcePacks/PlaceholderTextureCache.h"
#include "Core/ColorUtil.h"
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

bool LoadPlaceholderFile(const std::filesystem::path &path,
                         TexturePixelData &out)
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

UPlaceholderTextureCache::UPlaceholderTextureCache(
    std::filesystem::path cacheDir, int tileSize, glm::vec3 background,
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
      std::filesystem::remove(PlaceholderFilePath(CacheDir, stemIt->second),
                              ec);
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

std::string UPlaceholderTextureCache::MakeStem(const std::string &key) const
{
  const size_t h = std::hash<std::string>{}(key);
  std::ostringstream oss;
  oss << "__ph_" << std::hex << std::setw(8) << std::setfill('0')
      << static_cast<uint32_t>(h);
  return oss.str();
}

TexturePixelData
UPlaceholderTextureCache::Rasterize(const std::string &blockName, int faceIndex,
                                    int tileSize) const
{
  TexturePixelData out;
  const int size = std::max(1, tileSize);
  out.Width = size;
  out.Height = size;
  out.Rgba.resize(static_cast<size_t>(size * size * 4));

  // Keep placeholders unmistakable in-world: high-contrast warning stripes,
  // a yellow frame, and a large question mark. The configured background still
  // tints the warning color, so existing placeholder settings retain an effect.
  const uint8_t br = static_cast<uint8_t>(Background.r * 255.0f);
  const uint8_t bg = static_cast<uint8_t>(Background.g * 255.0f);
  const uint8_t bb = static_cast<uint8_t>(Background.b * 255.0f);
  const auto set_pixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b)
  {
    if (x < 0 || y < 0 || x >= size || y >= size)
    {
      return;
    }
    const size_t index = static_cast<size_t>((y * size + x) * 4);
    out.Rgba[index] = r;
    out.Rgba[index + 1] = g;
    out.Rgba[index + 2] = b;
    out.Rgba[index + 3] = 255;
  };
  const auto set_scaled_pixel = [&](int x, int y, int scale, uint8_t r,
                                    uint8_t g, uint8_t b)
  {
    for (int sy = 0; sy < scale; ++sy)
    {
      for (int sx = 0; sx < scale; ++sx)
      {
        set_pixel(x + sx, y + sy, r, g, b);
      }
    }
  };

  const uint8_t warning_r = static_cast<uint8_t>(
      std::clamp(0.75f * 255.0f + 0.25f * br, 0.0f, 255.0f));
  const uint8_t warning_g = static_cast<uint8_t>(
      std::clamp(0.25f * bg, 0.0f, 255.0f));
  const uint8_t warning_b = static_cast<uint8_t>(
      std::clamp(0.75f * 255.0f + 0.25f * bb, 0.0f, 255.0f));
  for (int y = 0; y < size; ++y)
  {
    for (int x = 0; x < size; ++x)
    {
      const bool stripe = ((x + y) / std::max(1, size / 5)) % 2 == 0;
      set_pixel(x, y, stripe ? warning_r : 18, stripe ? warning_g : 18,
                stripe ? warning_b : 18);
    }
  }

  // The border survives small texture filtering and makes the tile read as a
  // diagnostic marker even when the question mark is viewed from a distance.
  for (int i = 0; i < size; ++i)
  {
    set_pixel(i, 0, 255, 220, 32);
    set_pixel(i, size - 1, 255, 220, 32);
    set_pixel(0, i, 255, 220, 32);
    set_pixel(size - 1, i, 255, 220, 32);
  }

  static constexpr const char *kQuestionMark[] = {
      "11110", "00001", "00010", "00100", "00100", "00000", "00100"};
  const int scale = std::max(1, size / 8);
  const int glyph_width = 5 * scale;
  const int glyph_height = 7 * scale;
  const int origin_x = (size - glyph_width) / 2;
  const int origin_y = (size - glyph_height) / 2;
  for (int gy = 0; gy < 7; ++gy)
  {
    for (int gx = 0; gx < 5; ++gx)
    {
      if (kQuestionMark[gy][gx] != '1')
      {
        continue;
      }
      const int x = origin_x + gx * scale;
      const int y = origin_y + gy * scale;
      // Black outline keeps the mark legible against both stripe colors.
      for (int oy = -1; oy <= scale; ++oy)
      {
        for (int ox = -1; ox <= scale; ++ox)
        {
          set_pixel(x + ox, y + oy, 0, 0, 0);
        }
      }
      set_scaled_pixel(x, y, scale, 255, 255, 255);
    }
  }

  // These arguments remain part of the cache key for per-block/face identity.
  (void)blockName;
  (void)faceIndex;
  return out;
}

std::string
UPlaceholderTextureCache::GetOrCreateStem(const std::string &blockName,
                                          int faceIndex, int tileSize)
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
