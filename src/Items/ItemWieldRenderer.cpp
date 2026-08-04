#include "Items/ItemWieldRenderer.h"

namespace cutum
{

void UItemWieldRenderer::DrawMesh(const std::string &, float, float, float)
{
}

void UItemWieldRenderer::DrawFirstPerson(const InventoryEntryRef *active,
                                         const UItemDefinitionStorage *items,
                                         float aspect, bool broken_visual)
{
  // FP box arms + tool are drawn by UFpViewmodelRenderer::DrawOverlay
  // (Application RenderFrame). Full skinned mesh remains TD-ITEM-004.
  (void)active;
  (void)items;
  (void)aspect;
  (void)broken_visual;
}

} // namespace cutum
