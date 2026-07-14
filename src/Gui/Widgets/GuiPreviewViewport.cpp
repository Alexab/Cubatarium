#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

UGuiPreviewViewport::UGuiPreviewViewport(const GuiTheme *theme) : Theme(theme) {}

void UGuiPreviewViewport::SetPreviewTexture(GLuint texture)
{
  PreviewTexture = texture;
}

void UGuiPreviewViewport::SetOnRotationChanged(
    std::function<void(float yaw, float pitch)> handler)
{
  OnRotationChanged = std::move(handler);
}

void UGuiPreviewViewport::SetAngles(float yaw, float pitch)
{
  Yaw = yaw;
  Pitch = std::clamp(pitch, -80.0f, 80.0f);
}

void UGuiPreviewViewport::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, Theme->ButtonNormal);
  if (PreviewTexture != 0)
  {
    const int inset = Theme->Padding / 2;
    const GuiRect inner{Bounds.X + inset, Bounds.Y + inset,
                        Bounds.W - inset * 2, Bounds.H - inset * 2};
    const int texSize = std::min(inner.W, inner.H);
    const GuiRect texRect{inner.X + (inner.W - texSize) / 2,
                          inner.Y + (inner.H - texSize) / 2, texSize, texSize};
    renderer.DrawTexturedRect(texRect, PreviewTexture);
  }
  renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
}

bool UGuiPreviewViewport::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  Dragging = true;
  LastDragX = event.X;
  LastDragY = event.Y;
  return true;
}

bool UGuiPreviewViewport::OnMouseUp(const GuiMouseEvent &event)
{
  if (!Dragging)
  {
    return false;
  }
  Dragging = false;
  (void)event;
  return true;
}

bool UGuiPreviewViewport::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Dragging)
  {
    return false;
  }
  const int dx = event.X - LastDragX;
  const int dy = event.Y - LastDragY;
  LastDragX = event.X;
  LastDragY = event.Y;
  if (dx == 0 && dy == 0)
  {
    return true;
  }
  Yaw += static_cast<float>(dx) * 0.5f;
  Pitch = std::clamp(Pitch - static_cast<float>(dy) * 0.4f, -80.0f, 80.0f);
  if (OnRotationChanged)
  {
    OnRotationChanged(Yaw, Pitch);
  }
  return true;
}

} // namespace cutum
