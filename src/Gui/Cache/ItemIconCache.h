#ifndef ITEM_ICON_CACHE_H
#define ITEM_ICON_CACHE_H

#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Cache/IconWarmupQueue.h"
#include "Gui/Cache/InventoryIconService.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemVisualInputs.h"
#include "Render/Textures/GlTextureOwner.h"
#include <memory>
#include <string>
#include <unordered_map>

typedef unsigned int GLuint;

namespace cutum
{

class UItemPreviewRenderer;

/// Renders procedural 3D-ish tool icons into a texture cache (object-icon
/// style).
class UItemIconCache
{
public:
  explicit UItemIconCache(
      std::shared_ptr<UItemDefinitionStorage> items,
      std::shared_ptr<UInventoryIconService> iconService = nullptr,
      std::shared_ptr<UItemPreviewRenderer> itemPreview = nullptr);

  GLuint GetIcon(const std::string &itemId);
  GLuint GetIcon(const InventoryEntryRef &entry);
  void Invalidate();

  /// UX U3: per-frame budgeted icon generation. GetIcon for a not-yet-
  /// rendered item queues it (returning a shared placeholder) instead of
  /// rendering synchronously; WarmupNext drains disk reads and rendering
  /// within both an icon-count cap and a four-millisecond work budget.
  void WarmupNext(size_t count);
  size_t PendingWarmupCount() const { return Warmup.Size(); }

private:
  friend class UGuiIconSource;
  static constexpr int kIconSize = 64;
  /// Content identity of model, dependencies, shaders and icon presentation.
  std::string FingerprintFor(const std::string &itemId) const;
  uint64_t CachedRevision{UItemDefinitionStorage::ContentRevision()};
  bool SkipDiskCache{false};
  mutable std::unordered_map<std::string, std::string> Fingerprints;
  void SyncRevision();
  struct StateRequest
  {
    std::string Id, Variant;
    ItemVisualInputs Inputs;
  };
  std::unordered_map<std::string, StateRequest> StateRequests;
  GLuint RenderItemIcon(const std::string &itemId,
                        const ItemVisualInputs &inputs = {});
  void CompleteWarmupIcon(const std::string &itemId);

  std::shared_ptr<UItemDefinitionStorage> Items;
  std::shared_ptr<UInventoryIconService> IconService;
  std::shared_ptr<UItemPreviewRenderer> ItemPreview;
  std::unordered_map<std::string, UGlTextureOwner> Cache;
  UGuiIconWarmupQueue Warmup;
};

} // namespace cutum

#endif
