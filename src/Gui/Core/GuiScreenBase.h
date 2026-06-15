#ifndef GUI_SCREEN_BASE_H
#define GUI_SCREEN_BASE_H

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
  void SetViewportInsets(int insetLeft, int insetTop, int insetRight,
                         int insetBottom);
  virtual bool BlocksGameInput() const { return false; }

  UGuiWidget *GetRoot() { return root_.get(); }
  const UGuiWidget *GetRoot() const { return root_.get(); }

  int GetViewportWidth() const { return viewportW_; }
  int GetViewportHeight() const { return viewportH_; }
  int GetContentOffsetX() const { return contentOffsetX_; }
  int GetContentOffsetY() const { return contentOffsetY_; }

protected:
  std::unique_ptr<UGuiWidget> root_;
  int viewportW_{1280};
  int viewportH_{720};
  int contentOffsetX_{0};
  int contentOffsetY_{0};
  int contentInsetRight_{0};
  int contentInsetBottom_{0};
};

} // namespace cutum

#endif
