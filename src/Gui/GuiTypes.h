#ifndef GUI_TYPES_H
#define GUI_TYPES_H

namespace cutum {

struct GuiRect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};

    bool Contains(int px, int py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    bool Intersects(const GuiRect& other) const
    {
        return x < other.x + other.w && x + w > other.x &&
               y < other.y + other.h && y + h > other.y;
    }

    GuiRect Inset(int pad) const
    {
        return {x + pad, y + pad, w - 2 * pad, h - 2 * pad};
    }
};

enum class GuiMouseButton {
    Left,
    Right,
    Middle
};

enum class GuiKeyAction {
    Press,
    Release,
    Repeat
};

struct GuiMouseEvent {
    int x{0};
    int y{0};
    GuiMouseButton button{GuiMouseButton::Left};
    bool pressed{false};
};

struct GuiKeyEvent {
    int keyCode{0};
    GuiKeyAction action{GuiKeyAction::Press};
    int mods{0};
};

struct GuiCharEvent {
    unsigned int codepoint{0};
};

struct GuiScrollEvent {
    double xoffset{0.0};
    double yoffset{0.0};
};

} // namespace cutum

#endif
