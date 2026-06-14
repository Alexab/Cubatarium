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
  virtual bool BlocksGameInput() const { return false; }

  UGuiWidget *GetRoot() { return root_.get(); }
  const UGuiWidget *GetRoot() const { return root_.get(); }

  int GetViewportWidth() const { return viewportW_; }
  int GetViewportHeight() const { return viewportH_; }

protected:
  std::unique_ptr<UGuiWidget> root_;
  int viewportW_{1280};
  int viewportH_{720};
};

} // namespace cutum

#endif
