#ifndef GUI_TYPES_H
#define GUI_TYPES_H

namespace cutum
{

struct GuiRect
{
  int X{0};
  int Y{0};
  int W{0};
  int H{0};

  bool Contains(int px, int py) const
  {
    return px >= X && px < X + W && py >= Y && py < Y + H;
  }

  bool Intersects(const GuiRect &other) const
  {
    return X < other.X + other.W && X + W > other.X && Y < other.Y + other.H &&
           Y + H > other.Y;
  }

  GuiRect Inset(int pad) const
  {
    return {X + pad, Y + pad, W - 2 * pad, H - 2 * pad};
  }
};

enum class GuiMouseButton
{
  Left,
  Right,
  Middle
};

enum class GuiKeyAction
{
  Press,
  Release,
  Repeat
};

struct GuiMouseEvent
{
  int X{0};
  int Y{0};
  GuiMouseButton Button{GuiMouseButton::Left};
  bool Pressed{false};
  /// Android touch pointer index; -1 on desktop (matches any capture).
  int PointerId{-1};
};

inline bool GuiPointerMatches(int eventPointerId, int capturePointerId)
{
  return eventPointerId < 0 || capturePointerId < 0 ||
         eventPointerId == capturePointerId;
}

inline constexpr int kGuiTouchDragSlopPx = 14;

struct GuiKeyEvent
{
  int KeyCode{0};
  GuiKeyAction Action{GuiKeyAction::Press};
  int Mods{0};
};

struct GuiCharEvent
{
  unsigned int Codepoint{0};
};

struct GuiScrollEvent
{
  double Xoffset{0.0};
  double Yoffset{0.0};
};

} // namespace cutum

#endif
