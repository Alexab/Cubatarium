#ifndef GUI_SLOT_H
#define GUI_SLOT_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

struct GuiTheme;

class UGuiSlot : public UGuiWidget
{
public:
  explicit UGuiSlot(const GuiTheme *theme);

  void SetSelected(bool selected) { Selected = selected; }
  void SetLabel(const std::string &label) { Label = label; }
  void SetIconTexture(unsigned int texture) { IconTexture = texture; }
  void SetWearProgress(float wear01) { WearProgress = wear01; }
  void SetBroken(bool broken) { Broken = broken; }
  void SetCornerHint(const std::string &hint) { CornerHint = hint; }
  void SetDimmed(bool dimmed) { Dimmed = dimmed; }
  void SetOnClick(std::function<void()> handler)
  {
    OnClick = std::move(handler);
  }
  void SetOnBeginDrag(std::function<void()> handler)
  {
    OnBeginDrag = std::move(handler);
  }
  /// Force-clear press/drag tracking (inventory gesture teardown).
  void ClearPressState()
  {
    Pressed = false;
    DragStarted = false;
  }
  const std::string &GetLabel() const { return Label; }

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;

  int GetPreferredWidth() const override;
  int GetPreferredHeight() const override;

private:
  int SlotSizePx() const;
  int DragThresholdPx() const;

  const GuiTheme *Theme;
  bool Selected{false};
  bool Dimmed{false};
  std::string Label;
  std::string CornerHint;
  unsigned int IconTexture{0};
  float WearProgress{0.f};
  bool Broken{false};
  bool Pressed{false};
  bool DragStarted{false};
  int PressX{0};
  int PressY{0};
  std::function<void()> OnClick;
  std::function<void()> OnBeginDrag;
};

} // namespace cutum

#endif
