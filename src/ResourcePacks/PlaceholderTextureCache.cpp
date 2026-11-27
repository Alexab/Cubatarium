#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/PlaceholderPixelLayout.h"
#include "Core/ColorUtil.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <type_traits>

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
  if (!PlaceholderFileShapeFits(pixels.Width,pixels.Height,pixels.Rgba.size()))
    return false;
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
                         TexturePixelData &out, int tileSize)
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
  if (!in.good() || !PlaceholderHeaderMatches(width,height,size,tileSize))
  {
    return false;
  }
  // Reject truncated/mismatched payloads before allocating their advertised size.
  const auto payload=in.tellg();in.seekg(0,std::ios::end);const auto end=in.tellg();
  if(payload<0 || end<payload || static_cast<uint64_t>(end-payload)!=size)return false;
  in.seekg(payload);if(!in.good())return false;
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

void UPlaceholderTextureCache::EvictIfNeeded()
{
  while (LruOrder.size() > MaxEntries)
  {
    const auto &victim = LruOrder.back();
    // A hash-colliding stem can be referenced by another key. Keep that
    // shared payload/file until the last key is retired.
    const bool shared = std::any_of(LruOrder.begin(), std::prev(LruOrder.end()),
        [&](const auto &entry) { return entry.second == victim.second; });
    if (!shared)
    {
      std::error_code ec;
      std::filesystem::remove(PlaceholderFilePath(CacheDir, victim.second), ec);
      StemPixels.erase(victim.second);
    }
    LruIt.erase(victim.first);
    LruOrder.pop_back();
  }
}

std::string UPlaceholderTextureCache::MakeKey(const std::string &blockName,
                                              int faceIndex, int tileSize) const
{
  const auto face = std::to_string(faceIndex);
  const auto tile = std::to_string(tileSize);
  std::string key;
  const size_t suffix = face.size() + tile.size() + 2;
  if (blockName.size() > key.max_size() - suffix)
    throw std::length_error("placeholder key length overflow");
  key.reserve(blockName.size() + suffix);
  key.append(blockName).append("|").append(face).append("|").append(tile);
  return key;
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
  size_t bytes=0;
  if(!CheckedRgbaByteCount(static_cast<size_t>(size),static_cast<size_t>(size),bytes) || bytes>out.Rgba.max_size())
    throw std::length_error("placeholder pixel size overflow");
  out.Rgba.resize(bytes);

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
    const size_t index = (static_cast<size_t>(y)*static_cast<size_t>(size)+static_cast<size_t>(x))*4;
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
      const bool stripe = ((static_cast<size_t>(x)+static_cast<size_t>(y)) / static_cast<size_t>(std::max(1, size / 5))) % 2 == 0;
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
  auto key = MakeKey(blockName, faceIndex, tileSize);
  const auto it = LruIt.find(key);
  if (it != LruIt.end())
  {
    std::string result = it->second->second;
    LruOrder.splice(LruOrder.begin(), LruOrder, it->second);
    return result;
  }
  const std::string stem = MakeStem(key);
  TexturePixelData pixels;
  if (!LoadPlaceholderFile(PlaceholderFilePath(CacheDir, stem), pixels,tileSize))
  {
    pixels = Rasterize(blockName, faceIndex, tileSize);
    SavePlaceholderFile(PlaceholderFilePath(CacheDir, stem), pixels);
  }
  // Prepare the return string before admission. Returning this local moves
  // without allocation even when NRVO is unavailable.
  std::string result=stem;
  decltype(StemPixels)::iterator pixel;
  decltype(LruIt)::iterator lru;
  bool newPixel=false,newLru=false,listNode=false;
  try {
    const auto inserted=StemPixels.try_emplace(stem);pixel=inserted.first;newPixel=inserted.second;
    LruOrder.emplace_front(std::move(key),stem);listNode=true;
    const auto indexed=LruIt.emplace(LruOrder.front().first,LruOrder.begin());
    lru=indexed.first;newLru=indexed.second;
    // With a consistent cache one new key can evict at most one old key.
    // All allocation-capable file/path work precedes that victim's removal.
    EvictIfNeeded();
    static_assert(std::is_nothrow_move_assignable_v<TexturePixelData>);
    pixel->second=std::move(pixels);
  } catch (...) {
    if(newLru)LruIt.erase(lru);
    if(listNode)LruOrder.pop_front();
    if(newPixel)StemPixels.erase(pixel);
    throw;
  }
  return result;
}

void UPlaceholderTextureCache::RegisterAll(UTextureBaseStorage &storage) const
{
  for (const auto &entry : StemPixels)
  {
    storage.RegisterPixels(entry.first, entry.second);
  }
}

} // namespace cutum
