#ifndef ITEM_WIELD_RENDERER_H
#define ITEM_WIELD_RENDERER_H

#include <string>

namespace cutum
{

class UItemDefinitionStorage;
struct InventoryEntryRef;

/// First-person tool presentation helpers.
/// Screen-space box viewmodel lives in UFpViewmodelRenderer (ShowFpWield gate).
class UItemWieldRenderer
{
public:
  void DrawFirstPerson(const InventoryEntryRef *active,
                       const UItemDefinitionStorage *items, float aspect,
                       bool broken_visual);

private:
  void DrawMesh(const std::string &itemId, float r, float g, float b);
};

} // namespace cutum

#endif
