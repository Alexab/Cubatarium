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

  UGuiWidget *GetRoot() { return Root.get(); }
  const UGuiWidget *GetRoot() const { return Root.get(); }

  int GetViewportWidth() const { return ViewportW; }
  int GetViewportHeight() const { return ViewportH; }
  int GetContentOffsetX() const { return ContentOffsetX; }
  int GetContentOffsetY() const { return ContentOffsetY; }

protected:
  std::unique_ptr<UGuiWidget> Root;
  int ViewportW{1280};
  int ViewportH{720};
  int ContentOffsetX{0};
  int ContentOffsetY{0};
  int ContentInsetRight{0};
  int ContentInsetBottom{0};
};

} // namespace cutum

#endif
