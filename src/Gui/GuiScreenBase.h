#ifndef GUI_SCREEN_BASE_H
#define GUI_SCREEN_BASE_H

#include <memory>

namespace cutum {

class GuiContext;
class GuiWidget;

class GuiScreenBase {
public:
    virtual ~GuiScreenBase() = default;

    virtual void OnAttach(GuiContext& ctx);
    virtual void OnDetach();
    virtual void Build(GuiContext& ctx) = 0;
    virtual void Update(double dt);
    virtual void OnViewportChanged(int width, int height);
    virtual bool BlocksGameInput() const { return false; }

    GuiWidget* GetRoot() { return root_.get(); }
    const GuiWidget* GetRoot() const { return root_.get(); }

    int GetViewportWidth() const { return viewportW_; }
    int GetViewportHeight() const { return viewportH_; }

protected:
    std::unique_ptr<GuiWidget> root_;
    int viewportW_{1280};
    int viewportH_{720};
};

} // namespace cutum

#endif
