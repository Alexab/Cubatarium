#ifndef GUISCROLLBARCONTROLLER_H
#define GUISCROLLBARCONTROLLER_H

#include "Gui/Core/GuiTypes.h"

namespace cutum
{

enum class GuiScrollbarHit
{
  None,
  Thumb,
  TrackAbove,
  TrackBelow
};

struct GuiScrollbarMetrics
{
  GuiRect Track{};
  GuiRect Thumb{};
  int ViewportH{0};
  int MaxScroll{0};
  int ScrollY{0};
};

/// Desktop-style scrollbar interaction (thumb drag, track page jumps).
class UGuiScrollbarController
{
public:
  GuiScrollbarHit HitTest(const GuiScrollbarMetrics &metrics, int x,
                          int y) const;

  bool BeginThumbDrag(const GuiScrollbarMetrics &metrics, int mouseY);
  int ScrollFromThumbDrag(const GuiScrollbarMetrics &metrics, int mouseY) const;
  int ScrollFromTrackClick(const GuiScrollbarMetrics &metrics,
                           GuiScrollbarHit hit, int mouseY) const;

  void EndDrag();
  bool IsDragging() const { return DraggingThumb; }

private:
  static int ClampScroll(int scroll, int maxScroll);

  bool DraggingThumb{false};
  int GrabOffsetY{0};
};

} // namespace cutum

#endif
