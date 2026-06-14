#include "GuiFocus.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiScrollView.h"

namespace cutum
{

namespace
{

bool IsDescendantOf(const UGuiWidget *root, const UGuiWidget *target)
{
  if (!root || !target)
  {
    return false;
  }
  if (root == target)
  {
    return true;
  }
  for (const auto &child : root->GetChildren())
  {
    if (IsDescendantOf(child.get(), target))
    {
      return true;
    }
  }
  return false;
}

bool FindScrollAndReveal(UGuiWidget *node, UGuiWidget *target)
{
  if (!node)
  {
    return false;
  }
  if (auto *scroll = dynamic_cast<UGuiScrollView *>(node))
  {
    if (scroll->ContainsWidget(target))
    {
      scroll->EnsureWidgetVisible(*target);
      return true;
    }
    if (FindScrollAndReveal(&scroll->Content(), target))
    {
      return true;
    }
  }
  for (const auto &child : node->GetChildren())
  {
    if (FindScrollAndReveal(child.get(), target))
    {
      return true;
    }
  }
  return false;
}

} // namespace

void DrawWidgetFocusRing(UGuiRenderer &renderer, const GuiTheme &theme,
                         const GuiRect &bounds)
{
  if (bounds.w <= 0 || bounds.h <= 0)
  {
    return;
  }
  const int pad = theme.focusRingThickness;
  GuiRect ring{bounds.x - pad, bounds.y - pad, bounds.w + pad * 2,
               bounds.h + pad * 2};
  renderer.DrawBorderRect(ring, theme.focusRing, theme.focusRingThickness);
}

void RevealWidgetForKeyboardFocus(UGuiWidget *root, UGuiWidget *widget)
{
  if (!root || !widget)
  {
    return;
  }
  if (auto *list = dynamic_cast<UGuiListView *>(widget))
  {
    list->RevealFocused();
    return;
  }
  FindScrollAndReveal(root, widget);
}

} // namespace cutum
