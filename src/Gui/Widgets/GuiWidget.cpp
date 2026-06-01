#include "GuiWidget.h"
#include "Gui/GuiRenderer.h"

#include <algorithm>

namespace cutum {

void GuiWidget::UpdateLayout(const GuiRect& parentClientArea)
{
    if (bounds_.w <= 0 || bounds_.h <= 0) {
        bounds_ = parentClientArea;
    }
    const GuiRect childClient = bounds_.Inset(0);
    for (auto& child : children_) {
        child->UpdateLayout(childClient);
    }
}

void GuiWidget::Update(double /*dt*/)
{
    for (auto& child : children_) {
        child->Update(0.0);
    }
}

void GuiWidget::Draw(GuiRenderer& renderer)
{
    if (!visible_) {
        return;
    }
    std::vector<GuiWidget*> sorted;
    sorted.reserve(children_.size());
    for (auto& child : children_) {
        sorted.push_back(child.get());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const GuiWidget* a, const GuiWidget* b) { return a->GetZOrder() < b->GetZOrder(); });
    for (GuiWidget* child : sorted) {
        child->Draw(renderer);
    }
}

GuiWidget* GuiWidget::AddChild(std::unique_ptr<GuiWidget> child)
{
    child->UpdateLayout(bounds_);
    children_.push_back(std::move(child));
    return children_.back().get();
}

GuiWidget* GuiWidget::HitTest(int x, int y)
{
    if (!visible_ || !bounds_.Contains(x, y)) {
        return nullptr;
    }
    std::vector<GuiWidget*> sorted;
    for (auto& child : children_) {
        sorted.push_back(child.get());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const GuiWidget* a, const GuiWidget* b) { return a->GetZOrder() > b->GetZOrder(); });
    for (GuiWidget* child : sorted) {
        if (GuiWidget* hit = child->HitTest(x, y)) {
            return hit;
        }
    }
    return this;
}

bool GuiWidget::OnMouseDown(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseDown(event)) {
            return true;
        }
    }
    return false;
}

bool GuiWidget::OnMouseUp(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseUp(event)) {
            return true;
        }
    }
    return false;
}

bool GuiWidget::OnMouseMove(const GuiMouseEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnMouseMove(event)) {
            return true;
        }
    }
    return false;
}

bool GuiWidget::OnKey(const GuiKeyEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnKey(event)) {
            return true;
        }
    }
    return false;
}

bool GuiWidget::OnChar(const GuiCharEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnChar(event)) {
            return true;
        }
    }
    return false;
}

bool GuiWidget::OnScroll(const GuiScrollEvent& event)
{
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it)->OnScroll(event)) {
            return true;
        }
    }
    return false;
}

} // namespace cutum
