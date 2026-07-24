#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

UGuiCheckbox::UGuiCheckbox(const GuiTheme *theme, std::string label)
    : Theme(theme), Label(std::move(label))
{
}

int UGuiCheckbox::GetPreferredHeight() const
{
  return Theme ? Theme->FontSizeBody + Theme->Padding : 24;
}

bool UGuiCheckbox::CanFocus() const { return Enabled && Visible; }

bool UGuiCheckbox::Activate()
{
  if (!CanFocus())
  {
    return false;
  }
  Checked = !Checked;
  if (OnChanged)
  {
    OnChanged(Checked);
  }
  return true;
}

void UGuiCheckbox::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  const int box = Theme->FontSizeBody;
  GuiRect boxRect{Bounds.X, Bounds.Y + (Bounds.H - box) / 2, box, box};
  renderer.DrawFilledRect(boxRect,
                          Checked ? Theme->ButtonHover : Theme->ButtonNormal);
  renderer.DrawBorderRect(boxRect, Theme->PanelBorder, Theme->BorderThickness);
  if (Checked)
  {
    renderer.DrawTextCenteredInRect(boxRect, "x", Theme->TextPrimary);
  }
  renderer.DrawText(Label, Bounds.X + box + Theme->Padding,
                    Bounds.Y + Theme->Padding / 2, Theme->TextPrimary);
  if (HasFocusHighlight())
  {
    DrawWidgetFocusRing(renderer, *Theme, Bounds);
  }
}

bool UGuiCheckbox::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  if (event.Button != GuiMouseButton::Left)
  {
    return false;
  }
  return Activate();
}

bool UGuiCheckbox::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Visible || Description.empty() || !OnDescriptionHover)
  {
    return false;
  }
  const bool inside = Bounds.Contains(event.X, event.Y);
  if (inside == Hovered)
  {
    return inside;
  }
  Hovered = inside;
  OnDescriptionHover(inside ? Description : std::string{});
  return inside;
}

} // namespace cutum
