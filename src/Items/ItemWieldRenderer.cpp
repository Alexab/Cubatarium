#include "Items/ItemWieldRenderer.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace cutum
{

void UItemWieldRenderer::DrawMesh(const std::string &, float, float, float)
{
}

void UItemWieldRenderer::DrawFirstPerson(const InventoryEntryRef *active,
                                         const UItemDefinitionStorage *items,
                                         float aspect, bool broken_visual)
{
  (void)active;
  (void)items;
  (void)aspect;
  (void)broken_visual;
}

void DrawItemWieldOverlay(UGuiRenderer &renderer, const GuiTheme &theme,
                          IUGuiIconSource *icons, const InventoryEntryRef *active,
                          int framebuffer_w, int framebuffer_h)
{
  if (!icons || !active || active->empty ||
      active->kind != InventoryEntryKind::Item || active->Id.empty())
  {
    return;
  }
  const GLuint tex = icons->GetItemIconTexture(active->Id);
  if (tex == 0)
  {
    return;
  }
  const int size = std::max(72, theme.HotbarSlotSize * 2);
  const int x = framebuffer_w - size - theme.Padding * 3;
  const int y = framebuffer_h - size - theme.Padding * 6;
  GuiRect rect{x, y, size, size};
  if (active->broken)
  {
    renderer.DrawFilledRect(rect, theme.SlotDisabledFill);
  }
  renderer.DrawTexturedRect(rect, tex);
  if (active->wear > 0.01f && active->wear < 1.f)
  {
    const int barH = 4;
    GuiRect track{rect.X + 4, rect.Y + rect.H - barH - 4, rect.W - 8, barH};
    renderer.DrawFilledRect(track, glm::vec4(0.1f, 0.1f, 0.1f, 0.85f));
    const float remain = 1.f - active->wear;
    renderer.DrawFilledRect(
        {track.X, track.Y, static_cast<int>((rect.W - 8) * remain), barH},
        glm::vec4(0.25f, 0.85f, 0.3f, 1.f));
  }
}

} // namespace cutum
