#include "Gui/Cache/ItemIconCache.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Gui/Cache/ItemIconFingerprint.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Items/ItemRenderAsset.h"
#include "Render/GlIncludes.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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
  FillRect(px, size, size / 2 - 3, size / 2 - 4, size / 2 + 3, size - 8, 110,
           70, 30);
  if (id.find("sword") != std::string::npos)
  {
    FillRect(px, size, size / 2 - 3, 8, size / 2 + 3, size / 2, r, g, b);
    FillRect(px, size, size / 2 - 10, size / 2 - 2, size / 2 + 10, size / 2 + 3,
             r, g, b);
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
    std::shared_ptr<UInventoryIconService> iconService,
    std::shared_ptr<UItemPreviewRenderer> itemPreview)
    : Items(std::move(items)), IconService(std::move(iconService)),
      ItemPreview(std::move(itemPreview))
{
}

void UItemIconCache::EnsureFbo(int) {}

GLuint UItemIconCache::RenderItemIcon(const std::string &itemId,
                                      const ItemVisualInputs &inputs)
{
  constexpr int kSize = 64;

  if (ItemPreview)
  {
    const GLuint tex =
        ItemPreview->RenderToUniqueTexture(itemId, kSize, 45.f, 20.f, inputs);
    if (tex != 0)
    {
      return tex;
    }
  }

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

GLuint UItemIconCache::GetIcon(const InventoryEntryRef &entry)
{
  SyncRevision();
  if (entry.contentsId != "water" || entry.contentsUnits <= 0 || !Items)
    return GetIcon(entry.Id);
  const auto *def = Items->Get(entry.Id);
  const auto *paths = IUPlatformPaths::TryGet();
  if (!def || !paths || def->ModelPath.empty())
    return GetIcon(entry.Id);
  const auto asset =
      FindItemSurfaceAsset(paths->AssetRoot() / def->ModelPath,
                           UItemDefinitionStorage::ContentRevision());
  int maximum = 0;
  if (asset)
    for (const auto &b : asset->Bindings)
      if (b.Channel == ItemVisualChannel::FillUnits)
        maximum = std::max(maximum, int(std::ceil(b.Maximum)));
  if (maximum <= 0)
    return GetIcon(entry.Id);
  const int units = std::min(entry.contentsUnits, maximum);
  const std::string variant = "water:" + std::to_string(units),
                    key = entry.Id + "|" + variant;
  const auto found = Cache.find(key);
  if (found != Cache.end())
    return found->second;
  auto inputs = MakeItemVisualInputs(&entry);
  inputs.ContentsUnits = units;
  StateRequests[key] = {entry.Id, variant, inputs};
  Warmup.Push(key);
  return 0;
}

GLuint UItemIconCache::GetIcon(const std::string &itemId)
{
  SyncRevision();
  if (itemId.empty())
  {
    return 0;
  }
  const auto it = Cache.find(itemId);
  if (it != Cache.end())
  {
    return it->second;
  }
  // UX U3: queue for the per-frame drain instead of rendering synchronously
  // (glTF parse + FBO + glReadPixels + PNG write used to stall the frame).
  Warmup.Push(itemId);
  return 0;
}

void UItemIconCache::WarmupNext(size_t count)
{
  SyncRevision();
  size_t done = 0;
  const auto started = std::chrono::steady_clock::now();
  // Bound expensive disk validation, model loading and rendering together.
  // Always advance one queued icon; reopening the window is never required.
  constexpr auto budget = std::chrono::milliseconds(4);
  while (done < count &&
         (done == 0 || std::chrono::steady_clock::now() - started < budget))
  {
    const auto next = Warmup.PopFront();
    if (!next)
    {
      break;
    }
    const std::string itemId = *next;
    if (Cache.find(itemId) != Cache.end())
    {
      continue;
    }
    const auto state = StateRequests.find(itemId);
    const auto &definitionId =
        state == StateRequests.end() ? itemId : state->second.Id;
    ++done;
    if (IconService && !SkipDiskCache)
    {
      GLuint cached = 0;
      if (IconService->TryLoadIconTexture(
              "item", definitionId,
              state == StateRequests.end() ? "" : state->second.Variant,
              FingerprintFor(definitionId), kIconSize, cached) &&
          cached)
      {
        Cache[itemId] = cached;
        continue;
      }
    }
    const auto renderStart = std::chrono::steady_clock::now();
    const GLuint tex = RenderItemIcon(definitionId, state == StateRequests.end()
                                                        ? ItemVisualInputs{}
                                                        : state->second.Inputs);
    const auto renderEnd = std::chrono::steady_clock::now();
    Cache[itemId] = tex;
    auto fingerprintEnd = renderEnd;
    auto storeEnd = renderEnd;
    if (IconService)
    {
      const auto fingerprint = FingerprintFor(definitionId);
      fingerprintEnd = std::chrono::steady_clock::now();
      IconService->StoreIconTexture(
          "item", definitionId,
          state == StateRequests.end() ? "" : state->second.Variant,
          fingerprint, kIconSize, tex);
      storeEnd = std::chrono::steady_clock::now();
    }
    if (const char *path = std::getenv("CUBA_ICON_TIMING"))
    {
      const auto ms = [](auto a, auto b)
      { return std::chrono::duration<double, std::milli>(b - a).count(); };
      std::ofstream output(path, std::ios::app);
      output << nlohmann::json{{"id", definitionId},
                               {"render_ms", ms(renderStart, renderEnd)},
                               {"fingerprint_ms",
                                ms(renderEnd, fingerprintEnd)},
                               {"store_ms", ms(fingerprintEnd, storeEnd)},
                               {"disk_service", bool(IconService)}}
                    .dump()
             << '\n';
    }
  }
  if (IconService)
  {
    IconService->FlushManifest();
  }
}

std::string UItemIconCache::FingerprintFor(const std::string &itemId) const
{
  const auto cached = Fingerprints.find(itemId);
  if (cached != Fingerprints.end())
    return cached->second;
  const auto *def = Items ? Items->Get(itemId) : nullptr;
  const auto *paths = IUPlatformPaths::TryGet();
  std::string fingerprint;
  if (def && paths && !def->ModelPath.empty())
    fingerprint = BuildItemIconFingerprint(paths->AssetRoot(), def->ModelPath,
                                           def->Visual.IconScale);
  Fingerprints.emplace(itemId, fingerprint);
  return fingerprint;
}

void UItemIconCache::SyncRevision()
{
  if (CachedRevision != UItemDefinitionStorage::ContentRevision())
    Invalidate();
}

void UItemIconCache::Invalidate()
{
  CachedRevision = UItemDefinitionStorage::ContentRevision();
  SkipDiskCache = true;
  Warmup.Clear();
  StateRequests.clear();
  Fingerprints.clear();
  if (ItemPreview)
    ItemPreview->Invalidate();
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
