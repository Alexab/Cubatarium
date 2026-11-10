#ifndef ITEM_WIELD_RENDERER_H
#define ITEM_WIELD_RENDERER_H

#include <string>

namespace cutum
{

class UItemDefinitionStorage;
struct InventoryEntryRef;

/// Legacy facade — FP drawing is owned by UFpViewmodelRenderer.
class UItemWieldRenderer
{
public:
  void DrawFirstPerson(const InventoryEntryRef *active,
                       const UItemDefinitionStorage *items, float aspect,
                       bool broken_visual);
};

} // namespace cutum

#endif
