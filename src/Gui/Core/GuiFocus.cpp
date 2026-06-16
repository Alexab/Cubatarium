#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
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
  if (bounds.W <= 0 || bounds.H <= 0)
  {
    return;
  }
  const int pad = theme.FocusRingThickness;
  GuiRect ring{bounds.X - pad, bounds.Y - pad, bounds.W + pad * 2,
               bounds.H + pad * 2};
  renderer.DrawBorderRect(ring, theme.FocusRing, theme.FocusRingThickness);
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
