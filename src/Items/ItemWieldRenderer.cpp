#include "Items/ItemWieldRenderer.h"

namespace cutum
{

void UItemWieldRenderer::DrawFirstPerson(const InventoryEntryRef *active,
                                         const UItemDefinitionStorage *items,
                                         float aspect, bool broken_visual)
{
  // No-op: UFpViewmodelRenderer::DrawWorldOverlay is the sole FP path
  // (Application::RenderFrame after world paint).
  (void)active;
  (void)items;
  (void)aspect;
  (void)broken_visual;
}

} // namespace cutum
