#ifndef GUI_CONTEXT_H
#define GUI_CONTEXT_H

#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"
#include <functional>
#include <memory>
#include <vector>

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
  using MetricsChangedFn = std::function<void(const GuiMetrics &)>;

  UGuiContext();
  ~UGuiContext();

  bool Initialize(std::shared_ptr<UShaderManager> shaderManager,
                  std::shared_ptr<UTextRenderer> textRenderer);
  void Shutdown();

  void SetScreen(std::unique_ptr<UGuiScreenBase> screen);
  UGuiScreenBase *GetScreen() { return ActiveScreen.get(); }

  void Update(double dt);
  void NotifyViewport(int WindowWidth, int WindowHeight, int insetLeft = 0,
                      int insetTop = 0, int insetRight = 0,
                      int insetBottom = 0);
  void Render(int WindowWidth, int WindowHeight, int insetLeft = 0,
              int insetTop = 0, int insetRight = 0, int insetBottom = 0);
  void RenderOverlay(UGuiWidget &root, int WindowWidth, int WindowHeight,
                     bool expandRootToViewport = true);

  bool RouteKey(const GuiKeyEvent &event);
  bool RouteChar(const GuiCharEvent &event);
  bool RouteMouseDown(const GuiMouseEvent &event);
  bool RouteMouseUp(const GuiMouseEvent &event);
  bool RouteMouseMove(const GuiMouseEvent &event);
  bool RouteScroll(const GuiScrollEvent &event, int mouseX, int mouseY);

  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;
  void ClearInputState();

  UGuiRenderer &GetRenderer() { return *Renderer; }
  const GuiTheme &GetTheme() const { return Theme; }
  const GuiMetrics &GetMetrics() const { return Metrics; }

  void SetClipboard(IGuiClipboard *clipboard) { Clipboard = clipboard; }
  IGuiClipboard *GetClipboard() const { return Clipboard; }

  void ApplyUiScale(float scale);
  float GetUiScale() const { return UiScale; }

  void AddMetricsChangedListener(MetricsChangedFn listener);
  void ClearMetricsChangedListeners();

private:
  void NotifyMetricsChanged();

  GuiTheme BaseTheme;
  float UiScale{1.f};
  GuiMetrics Metrics;
  IGuiClipboard *Clipboard{nullptr};
  GuiTheme Theme;
  std::unique_ptr<UGuiRenderer> Renderer;
  std::unique_ptr<UGuiInputRouter> InputRouter;
  std::unique_ptr<UGuiScreenBase> ActiveScreen;
  std::vector<MetricsChangedFn> MetricsListeners;
};

} // namespace cutum

#endif
