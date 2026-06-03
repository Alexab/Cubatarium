#ifndef GUI_CONTEXT_H
#define GUI_CONTEXT_H

#include "GuiTypes.h"
#include "GuiTheme.h"
#include <memory>

namespace cutum {

class GuiRenderer;
class GuiInputRouter;
class GuiScreenBase;
class GuiWidget;
class IGuiClipboard;
class ShaderManager;
class TextRenderer;

class GuiContext {
public:
    GuiContext();
    ~GuiContext();

    bool Initialize(std::shared_ptr<ShaderManager> shaderManager,
                    std::shared_ptr<TextRenderer> textRenderer);
    void Shutdown();

    void SetScreen(std::unique_ptr<GuiScreenBase> screen);
    GuiScreenBase* GetScreen() { return activeScreen_.get(); }

    void Update(double dt);
    void Render(int windowWidth, int windowHeight);
    void RenderOverlay(GuiWidget& root, int windowWidth, int windowHeight,
                       bool expandRootToViewport = true);
    void NotifyViewport(int windowWidth, int windowHeight);

    bool RouteKey(const GuiKeyEvent& event);
    bool RouteChar(const GuiCharEvent& event);
    bool RouteMouseDown(const GuiMouseEvent& event);
    bool RouteMouseUp(const GuiMouseEvent& event);
    bool RouteMouseMove(const GuiMouseEvent& event);
    bool RouteScroll(const GuiScrollEvent& event, int mouseX, int mouseY);

    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;
    void ClearInputState();

    GuiRenderer& GetRenderer() { return *renderer_; }
    const GuiTheme& GetTheme() const { return theme_; }

    void SetClipboard(IGuiClipboard* clipboard) { clipboard_ = clipboard; }
    IGuiClipboard* GetClipboard() const { return clipboard_; }

private:
    IGuiClipboard* clipboard_{nullptr};
    GuiTheme theme_;
    std::unique_ptr<GuiRenderer> renderer_;
    std::unique_ptr<GuiInputRouter> inputRouter_;
    std::unique_ptr<GuiScreenBase> activeScreen_;
};

} // namespace cutum

#endif
