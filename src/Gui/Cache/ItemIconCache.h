#ifndef ITEM_ICON_CACHE_H
#define ITEM_ICON_CACHE_H

#include "Gui/Cache/IconWarmupQueue.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Items/ItemDefinitionStorage.h"
#include <memory>
#include <string>
#include <unordered_map>

typedef unsigned int GLuint;

namespace cutum
{

class UItemPreviewRenderer;

/// Renders procedural 3D-ish tool icons into a texture cache (object-icon style).
class UItemIconCache
{
public:
  explicit UItemIconCache(
      std::shared_ptr<UItemDefinitionStorage> items,
      std::shared_ptr<UInventoryIconService> iconService = nullptr,
      std::shared_ptr<UItemPreviewRenderer> itemPreview = nullptr);

  GLuint GetIcon(const std::string &itemId);
  void Invalidate();

  /// UX U3: per-frame budgeted icon generation. GetIcon for a not-yet-
  /// rendered item queues it (returning a shared placeholder) instead of
  /// rendering synchronously; WarmupNext drains the queue.
  void WarmupNext(size_t count);
  size_t PendingWarmupCount() const { return Warmup.Size(); }

private:
  static constexpr int kIconSize = 64;
  /// Stable fingerprint for the disk cache: item id + its model preset so
  /// stale PNGs re-render instead of showing wrong art.
  std::string FingerprintFor(const std::string &itemId) const;
  GLuint RenderItemIcon(const std::string &itemId);
  void EnsureFbo(int size);

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UInventoryIconService> IconService;
  std::shared_ptr<UItemPreviewRenderer> ItemPreview;
  std::unordered_map<std::string, GLuint> Cache;
  UGuiIconWarmupQueue Warmup;
  GLuint Fbo{0};
  GLuint ColorTex{0};
  GLuint DepthRbo{0};
  int FboSize{0};
};

} // namespace cutum

#endif
