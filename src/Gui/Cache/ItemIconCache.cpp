#include "Gui/Cache/ItemIconCache.h"
#include "Render/GlIncludes.h"
#include <algorithm>
#include <vector>

namespace cutum
{

namespace
{

void ToolRgb(const std::string &id, unsigned char &r, unsigned char &g,
             unsigned char &b)
{
  if (id.find("iron") != std::string::npos)
  {
    r = 185;
    g = 190;
    b = 200;
    return;
  }
  if (id.find("stone") != std::string::npos)
  {
    r = 140;
    g = 140;
    b = 132;
    return;
  }
  r = 158;
  g = 107;
  b = 56;
}

void FillRect(std::vector<unsigned char> &px, int size, int x0, int y0, int x1,
              int y1, unsigned char r, unsigned char g, unsigned char b,
              unsigned char a = 255)
{
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(size, x1);
  y1 = std::min(size, y1);
  for (int y = y0; y < y1; ++y)
  {
    for (int x = x0; x < x1; ++x)
    {
      const size_t i = static_cast<size_t>((y * size + x) * 4);
      px[i] = r;
      px[i + 1] = g;
      px[i + 2] = b;
      px[i + 3] = a;
    }
  }
}

void PaintTool(std::vector<unsigned char> &px, int size, const std::string &id)
{
  unsigned char r = 158, g = 107, b = 56;
  ToolRgb(id, r, g, b);
  // handle
  FillRect(px, size, size / 2 - 3, size / 2 - 4, size / 2 + 3, size - 8, 110, 70,
           30);
  if (id.find("sword") != std::string::npos)
  {
    FillRect(px, size, size / 2 - 3, 8, size / 2 + 3, size / 2, r, g, b);
    FillRect(px, size, size / 2 - 10, size / 2 - 2, size / 2 + 10, size / 2 + 3, r,
             g, b);
  }
  else if (id.find("shovel") != std::string::npos)
  {
    FillRect(px, size, size / 2 - 8, 10, size / 2 + 8, 26, r, g, b);
  }
  else if (id.find("axe") != std::string::npos)
  {
    FillRect(px, size, size / 2, 10, size / 2 + 14, 28, r, g, b);
  }
  else
  {
    // pickaxe head
    FillRect(px, size, size / 2 - 14, 12, size / 2 + 14, 20, r, g, b);
    FillRect(px, size, size / 2 - 14, 12, size / 2 - 8, 28, r, g, b);
    FillRect(px, size, size / 2 + 8, 12, size / 2 + 14, 28, r, g, b);
  }
}

} // namespace

UItemIconCache::UItemIconCache(
    std::shared_ptr<UItemDefinitionStorage> items,
    std::shared_ptr<UInventoryIconService> iconService)
    : Items(std::move(items)), IconService(std::move(iconService))
{
}

void UItemIconCache::EnsureFbo(int)
{
}

GLuint UItemIconCache::RenderItemIcon(const std::string &itemId)
{
  constexpr int kSize = 64;
  std::vector<unsigned char> pixels(static_cast<size_t>(kSize * kSize * 4), 0);
  PaintTool(pixels, kSize, itemId);

  GLuint outTex = 0;
  glGenTextures(1, &outTex);
  glBindTexture(GL_TEXTURE_2D, outTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return outTex;
}

GLuint UItemIconCache::GetIcon(const std::string &itemId)
{
  if (itemId.empty())
  {
    return 0;
  }
  const auto it = Cache.find(itemId);
  if (it != Cache.end())
  {
    return it->second;
  }
  const GLuint tex = RenderItemIcon(itemId);
  Cache[itemId] = tex;
  if (IconService)
  {
    IconService->StoreIconTexture("item", itemId, "", itemId, 64, tex);
  }
  return tex;
}

void UItemIconCache::Invalidate()
{
  for (auto &pair : Cache)
  {
    if (pair.second)
    {
      glDeleteTextures(1, &pair.second);
    }
  }
  Cache.clear();
}

} // namespace cutum
