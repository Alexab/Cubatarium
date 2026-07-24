#ifndef GUI_PREVIEW_VIEWPORT_H
#define GUI_PREVIEW_VIEWPORT_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>

typedef unsigned int GLuint;

namespace cutum
{

struct GuiTheme;

class UGuiPreviewViewport : public UGuiWidget
{
public:
  explicit UGuiPreviewViewport(const GuiTheme *theme);

  void SetPreviewTexture(GLuint texture);
  void SetOnRotationChanged(std::function<void(float yaw, float pitch)> handler);

  float GetYaw() const { return Yaw; }
  float GetPitch() const { return Pitch; }
  void SetAngles(float yaw, float pitch);

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

private:
  const GuiTheme *Theme;
  GLuint PreviewTexture{0};
  bool Dragging{false};
  int LastDragX{0};
  int LastDragY{0};
  float Yaw{45.0f};
  float Pitch{32.0f};
  std::function<void(float, float)> OnRotationChanged;
};

} // namespace cutum

#endif
