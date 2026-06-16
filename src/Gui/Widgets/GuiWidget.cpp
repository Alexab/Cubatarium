#include "Gui/Widgets/GuiWidget.h"
#include "Gui/Core/GuiRenderer.h"

#include <algorithm>

namespace cutum
{

void UGuiWidget::UpdateLayout(const GuiRect &parentClientArea)
{
  if (Bounds.W <= 0 || Bounds.H <= 0)
  {
    Bounds = parentClientArea;
  }
  const GuiRect childClient = Bounds.Inset(0);
  for (auto &child : Children)
  {
    child->UpdateLayout(childClient);
  }
}

void UGuiWidget::Update(double /*dt*/)
{
  for (auto &child : Children)
  {
    child->Update(0.0);
  }
}

void UGuiWidget::Draw(UGuiRenderer &renderer)
{
  if (!Visible)
  {
    return;
  }
  std::vector<UGuiWidget *> sorted;
  sorted.reserve(Children.size());
  for (auto &child : Children)
  {
    sorted.push_back(child.get());
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const UGuiWidget *a, const UGuiWidget *b)
            { return a->GetZOrder() < b->GetZOrder(); });
  for (UGuiWidget *child : sorted)
  {
    child->Draw(renderer);
  }
}

UGuiWidget *UGuiWidget::AddChild(std::unique_ptr<UGuiWidget> child)
{
  child->UpdateLayout(Bounds);
  Children.push_back(std::move(child));
  return Children.back().get();
}

void UGuiWidget::ClearChildren() { Children.clear(); }

bool UGuiWidget::Activate() { return false; }

void UGuiWidget::CollectFocusables(std::vector<UGuiWidget *> &out)
{
  if (!Visible || !Enabled)
  {
    return;
  }
  if (CanFocus())
  {
    out.push_back(this);
    return;
  }
  for (auto &child : Children)
  {
    child->CollectFocusables(out);
  }
}

UGuiWidget *UGuiWidget::HitTest(int x, int y)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return nullptr;
  }
  std::vector<UGuiWidget *> sorted;
  for (auto &child : Children)
  {
    sorted.push_back(child.get());
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const UGuiWidget *a, const UGuiWidget *b)
            { return a->GetZOrder() > b->GetZOrder(); });
  for (UGuiWidget *child : sorted)
  {
    if (UGuiWidget *hit = child->HitTest(x, y))
    {
      return hit;
    }
  }
  return this;
}

UGuiWidget *UGuiWidget::HitTestFocusable(int x, int y)
{
  if (!Visible || !Enabled || !Bounds.Contains(x, y))
  {
    return nullptr;
  }
  UGuiWidget *found = nullptr;
  for (auto &child : Children)
  {
    if (UGuiWidget *hit = child->HitTestFocusable(x, y))
    {
      found = hit;
    }
  }
  if (found)
  {
    return found;
  }
  return CanFocus() ? this : nullptr;
}

bool UGuiWidget::OnMouseDown(const GuiMouseEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnMouseDown(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::OnMouseUp(const GuiMouseEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnMouseUp(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::OnMouseMove(const GuiMouseEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnMouseMove(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::OnKey(const GuiKeyEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnKey(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::OnChar(const GuiCharEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnChar(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::OnScroll(const GuiScrollEvent &event)
{
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->OnScroll(event))
    {
      return true;
    }
  }
  return false;
}

bool UGuiWidget::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return false;
  }
  for (auto it = Children.rbegin(); it != Children.rend(); ++it)
  {
    if ((*it)->ScrollAtPoint(x, y, event))
    {
      return true;
    }
  }
  return OnScroll(event);
}

} // namespace cutum
