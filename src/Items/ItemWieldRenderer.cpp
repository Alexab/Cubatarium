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
  // FP box arms + tool are drawn by UFpViewmodelRenderer::DrawWorldOverlay
  // (Application RenderFrame, after world paint). Skinned arms = TD-ITEM-004.
  (void)active;
  (void)items;
  (void)aspect;
  (void)broken_visual;
}

} // namespace cutum
