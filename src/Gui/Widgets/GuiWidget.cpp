#include "GuiWidget.h"
#include "Gui/GuiRenderer.h"

#include <algorithm>

namespace cutum {

void UGuiWidget::UpdateLayout(const GuiRect& parentClientArea)
{
    if (bounds_.w <= 0 || bounds_.h <= 0) {
        bounds_ = parentClientArea;
    }
    const GuiRect childClient = bounds_.Inset(0);
    for (auto& child : children_) {
        child->UpdateLayout(childClient);
    }
}

void UGuiWidget::Update(double /*dt*/)
{
    for (auto& child : children_) {
        child->Update(0.0);
    }
}

void UGuiWidget::Draw(UGuiRenderer& renderer)
{
    if (!visible_) {
        return;
    }
    std::vector<UGuiWidget*> sorted;
    sorted.reserve(children_.size());
    for (auto& child : children_) {
        sorted.push_back(child.get());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const UGuiWidget* a, const UGuiWidget* b) { return a->GetZOrder() < b->GetZOrder(); });
    for (UGuiWidget* child : sorted) {
        child->Draw(renderer);
    }
}

UGuiWidget* UGuiWidget::AddChild(std::unique_ptr<UGuiWidget> child)
{
    child->UpdateLayout(bounds_);
    children_.push_back(std::move(child));
    return children_.back().get();
}

void UGuiWidget::ClearChildren()
{
    children_.clear();
}

bool UGuiWidget::Activate()
{
    return false;
}

void UGuiWidget::CollectFocusables(std::vector<UGuiWidget*>& out)
{
    if (!visible_ || !enabled_) {
        return;
    }
    if (CanFocus()) {
        out.push_back(this);
        return;
    }
    for (auto& child : children_) {
        child->CollectFocusables(out);
    }
}

UGuiWidget* UGuiWidget::HitTest(int x, int y)
{
    if (!visible_ || !bounds_.Contains(x, y)) {
        return nullptr;
    }
    std::vector<UGuiWidget*> sorted;
    for (auto& child : children_) {
        sorted.push_back(child.get());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const UGuiWidget* a, const UGuiWidget* b) { return a->GetZOrder() > b->GetZOrder(); });
    for (UGuiWidget* child : sorted) {
        if (UGuiWidget* hit = child->HitTest(x, y)) {
            return hit;
        }
    }
    return this;
}

UGuiWidget* UGuiWidget::HitTestFocusable(int x, int y)
{
    if (!visible_ || !enabled_ || !bounds_.Contains(x, y)) {
        return nullptr;
    }
    UGuiWidget* found = nullptr;
    for (auto& child : children_) {
        if (UGuiWidget* hit = child->HitTestFocusable(x, y)) {
            found = hit;
        }
    }
    if (found) {
        return found;
    }
    return CanFocus() ? this : nullptr;
}

bool UGuiWidget::OnMouseDown(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseDown(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::OnMouseUp(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseUp(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::OnMouseMove(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseMove(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::OnKey(const GuiKeyEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnKey(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::OnChar(const GuiCharEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnChar(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::OnScroll(const GuiScrollEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnScroll(event)) {
            return true;
        }
    }
    return false;
}

bool UGuiWidget::ScrollAtPoint(int x, int y, const GuiScrollEvent& event)
{
    if (!visible_ || !bounds_.Contains(x, y)) {
        return false;
    }
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->ScrollAtPoint(x, y, event)) {
            return true;
        }
    }
    return OnScroll(event);
}

} // namespace cutum
