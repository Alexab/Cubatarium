#ifndef GUI_WIDGET_H
#define GUI_WIDGET_H

#include "Gui/GuiTypes.h"
#include <memory>
#include <vector>

namespace cutum {

class GuiRenderer;

class GuiWidget {
public:
    virtual ~GuiWidget() = default;

    void SetBounds(const GuiRect& bounds) { bounds_ = bounds; }
    const GuiRect& GetBounds() const { return bounds_; }

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    void SetZOrder(int z) { zOrder_ = z; }
    int GetZOrder() const { return zOrder_; }

    virtual int GetPreferredWidth() const { return bounds_.w > 0 ? bounds_.w : 100; }
    virtual int GetPreferredHeight() const { return bounds_.h > 0 ? bounds_.h : 32; }

    virtual void UpdateLayout(const GuiRect& parentClientArea);
    virtual void Update(double dt);
    virtual void Draw(GuiRenderer& renderer);

    GuiWidget* HitTest(int x, int y);
    const std::vector<std::unique_ptr<GuiWidget>>& GetChildren() const { return children_; }

    GuiWidget* AddChild(std::unique_ptr<GuiWidget> child);

    virtual bool OnMouseDown(const GuiMouseEvent& event);
    virtual bool OnMouseUp(const GuiMouseEvent& event);
    virtual bool OnMouseMove(const GuiMouseEvent& event);
    virtual bool OnKey(const GuiKeyEvent& event);
    virtual bool OnChar(const GuiCharEvent& event);
    virtual bool OnScroll(const GuiScrollEvent& event);

protected:
    GuiRect bounds_{0, 0, 100, 32};
    bool visible_{true};
    bool enabled_{true};
    int zOrder_{0};
    std::vector<std::unique_ptr<GuiWidget>> children_;
};

} // namespace cutum

#endif
