#ifndef ITEM_WIELD_RENDERER_H
#define ITEM_WIELD_RENDERER_H

#include <string>

namespace cutum
{

class UItemDefinitionStorage;
class UGuiRenderer;
class IUGuiIconSource;
struct GuiTheme;
struct InventoryEntryRef;

/// First-person tool presentation helpers.
class UItemWieldRenderer
{
public:
  void DrawFirstPerson(const InventoryEntryRef *active,
                       const UItemDefinitionStorage *items, float aspect,
                       bool broken_visual);

private:
  void DrawMesh(const std::string &itemId, float r, float g, float b);
};

/// Screen-space FP viewmodel overlay (icon billboard until full mesh path).
void DrawItemWieldOverlay(UGuiRenderer &renderer, const GuiTheme &theme,
                          IUGuiIconSource *icons, const InventoryEntryRef *active,
                          int framebuffer_w, int framebuffer_h);

} // namespace cutum

#endif
