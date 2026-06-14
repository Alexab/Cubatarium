#ifndef GUI_CONTEXT_H
#define GUI_CONTEXT_H

#include "GuiTheme.h"
#include "GuiTypes.h"
#include <memory>

namespace cutum
{

class UGuiRenderer;
class UGuiInputRouter;
class UGuiScreenBase;
class UGuiWidget;
class IGuiClipboard;
class UShaderManager;
class UTextRenderer;

class UGuiContext
{
public:
  UGuiContext();
  ~UGuiContext();

  bool Initialize(std::shared_ptr<UShaderManager> shaderManager,
                  std::shared_ptr<UTextRenderer> textRenderer);
  void Shutdown();

  void SetScreen(std::unique_ptr<UGuiScreenBase> screen);
  UGuiScreenBase *GetScreen() { return activeScreen_.get(); }

  void Update(double dt);
  void Render(int WindowWidth, int WindowHeight);
  void RenderOverlay(UGuiWidget &root, int WindowWidth, int WindowHeight,
                     bool expandRootToViewport = true);
  void NotifyViewport(int WindowWidth, int WindowHeight);

  bool RouteKey(const GuiKeyEvent &event);
  bool RouteChar(const GuiCharEvent &event);
  bool RouteMouseDown(const GuiMouseEvent &event);
  bool RouteMouseUp(const GuiMouseEvent &event);
  bool RouteMouseMove(const GuiMouseEvent &event);
  bool RouteScroll(const GuiScrollEvent &event, int mouseX, int mouseY);

  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;
  void ClearInputState();

  UGuiRenderer &GetRenderer() { return *renderer_; }
  const GuiTheme &GetTheme() const { return theme_; }

  void SetClipboard(IGuiClipboard *clipboard) { Clipboard = clipboard; }
  IGuiClipboard *GetClipboard() const { return Clipboard; }

private:
  IGuiClipboard *Clipboard{nullptr};
  GuiTheme theme_;
  std::unique_ptr<UGuiRenderer> renderer_;
  std::unique_ptr<UGuiInputRouter> inputRouter_;
  std::unique_ptr<UGuiScreenBase> activeScreen_;
};

} // namespace cutum

#endif
