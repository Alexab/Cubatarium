#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

#include <algorithm>

namespace cutum
{

UGuiPopupMenu::UGuiPopupMenu(const GuiTheme *theme) : Theme(theme)
{
  SetVisible(false);
  SetZOrder(1000);
}

void UGuiPopupMenu::SetItems(std::vector<GuiPopupMenuItem> items)
{
  Items = std::move(items);
}

int UGuiPopupMenu::ItemHeight() const
{
  return Theme ? Theme->FontSizeBody + Theme->Padding * 2 : 28;
}

int UGuiPopupMenu::MenuWidth(int viewportW) const
{
  if (!Theme)
  {
    return 120;
  }
  int maxW = 80;
  for (const auto &item : Items)
  {
    const int w = Theme->Padding * 2 + static_cast<int>(item.label.size()) *
                                           (Theme->FontSizeBody / 2 + 4);
    maxW = std::max(maxW, w);
  }
  return std::min(maxW, viewportW - 8);
}

void UGuiPopupMenu::OpenAt(int x, int y, int viewportW, int viewportH)
{
  if (Items.empty() || !Theme)
  {
    return;
  }
  const int w = MenuWidth(viewportW);
  const int h = static_cast<int>(Items.size()) * ItemHeight();
  int px = x;
  int py = y;
  if (px + w > viewportW)
  {
    px = std::max(0, viewportW - w);
  }
  if (py + h > viewportH)
  {
    py = std::max(0, viewportH - h);
  }
  SetBounds({px, py, w, h});
  HoverIndex = -1;
  Open = true;
  SetVisible(true);
}

void UGuiPopupMenu::Close()
{
  Open = false;
  SetVisible(false);
  HoverIndex = -1;
}

int UGuiPopupMenu::ItemIndexAt(int x, int y) const
{
  if (!Open || !Bounds.Contains(x, y))
  {
    return -1;
  }
  const int localY = y - Bounds.Y;
  const int idx = localY / ItemHeight();
  if (idx < 0 || idx >= static_cast<int>(Items.size()))
  {
    return -1;
  }
  return idx;
}

UGuiWidget *UGuiPopupMenu::HitTest(int x, int y)
{
  if (!Open || !Visible)
  {
    return nullptr;
  }
  if (Bounds.Contains(x, y))
  {
    return this;
  }
  return nullptr;
}

bool UGuiPopupMenu::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Open)
  {
    return false;
  }
  HoverIndex = ItemIndexAt(event.X, event.Y);
  return Bounds.Contains(event.X, event.Y);
}

bool UGuiPopupMenu::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Open || event.Button != GuiMouseButton::Left)
  {
    return false;
  }
  if (!Bounds.Contains(event.X, event.Y))
  {
    Close();
    return true;
  }
  const int idx = ItemIndexAt(event.X, event.Y);
  if (idx >= 0 && idx < static_cast<int>(Items.size()) &&
      Items[static_cast<size_t>(idx)].enabled)
  {
    if (Items[static_cast<size_t>(idx)].Action)
    {
      Items[static_cast<size_t>(idx)].Action();
    }
    Close();
    return true;
  }
  Close();
  return true;
}

void UGuiPopupMenu::Draw(UGuiRenderer &renderer)
{
  if (!Open || !Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, Theme->PanelBackground);
  renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  int y = Bounds.Y;
  const int rowH = ItemHeight();
  for (size_t i = 0; i < Items.size(); ++i)
  {
    GuiRect row{Bounds.X, y, Bounds.W, rowH};
    if (static_cast<int>(i) == HoverIndex)
    {
      renderer.DrawFilledRect(row, Theme->ButtonHover);
    }
    const glm::vec3 color =
        Items[i].enabled ? Theme->TextPrimary : Theme->TextSecondary;
    renderer.DrawText(Items[i].label, row.X + Theme->Padding,
                      row.Y + Theme->Padding, color);
    y += rowH;
  }
}

} // namespace cutum
