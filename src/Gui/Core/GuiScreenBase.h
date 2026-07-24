#ifndef GUI_SCREEN_BASE_H
#define GUI_SCREEN_BASE_H

#include "Gui/Core/GuiMetrics.h"
#include <memory>

namespace cutum
{

class UGuiContext;
class UGuiWidget;

class UGuiScreenBase
{
public:
  virtual ~UGuiScreenBase();

  virtual void OnAttach(UGuiContext &ctx);
  virtual void OnDetach();
  virtual void Build(UGuiContext &ctx) = 0;
  virtual void Update(double dt);
  virtual void OnViewportChanged(int width, int height);
  virtual void OnMetricsChanged(const GuiMetrics &metrics);
  void SetViewportInsets(int insetLeft, int insetTop, int insetRight,
                         int insetBottom);
  virtual bool BlocksGameInput() const { return false; }

  UGuiWidget *GetRoot() { return Root.get(); }
  const UGuiWidget *GetRoot() const { return Root.get(); }

  int GetViewportWidth() const { return ViewportW; }
  int GetViewportHeight() const { return ViewportH; }
  int GetContentOffsetX() const { return ContentOffsetX; }
  int GetContentOffsetY() const { return ContentOffsetY; }
  const GuiMetrics &GetMetrics() const { return Metrics; }
  UGuiContext *GetContext() const { return Context; }

protected:
  int Scaled(int design_px) const;
  void RelayoutOnMetricsChange();

  std::unique_ptr<UGuiWidget> Root;
  UGuiContext *Context{nullptr};
  GuiMetrics Metrics;
  int ViewportW{1280};
  int ViewportH{720};
  int ContentOffsetX{0};
  int ContentOffsetY{0};
  int ContentInsetRight{0};
  int ContentInsetBottom{0};
  int LastFrameWidth{0};
  int LastFrameHeight{0};
};

} // namespace cutum

#endif
