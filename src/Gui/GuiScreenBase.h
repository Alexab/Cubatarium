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
    virtual bool BlocksGameInput() const { return false; }

    GuiWidget* GetRoot() { return root_.get(); }
    const GuiWidget* GetRoot() const { return root_.get(); }

protected:
    std::unique_ptr<GuiWidget> root_;
};

} // namespace cutum

#endif
