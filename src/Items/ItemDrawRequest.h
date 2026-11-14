#ifndef ITEM_DRAW_REQUEST_H
#define ITEM_DRAW_REQUEST_H

#include "Items/ItemRenderAsset.h"
#include <optional>

namespace cutum
{
/// Immutable item-only geometry/pose payload carried by a shared draw queue.
/// Inventory ownership is deliberately absent.
struct ItemDrawRequest
{
  std::shared_ptr<const ItemRenderAsset> Asset;
  std::filesystem::path Path;
  glm::mat4 ViewProjection{1.f}, Model{1.f};
  ItemVisualState State;
  glm::vec3 Low{0.f}, High{0.f};
  /// Loose bodies sample their own cell; equipment inherits its carrier's
  /// light.
  std::optional<glm::vec3> LightSamplePosition;
};
} // namespace cutum
#endif
