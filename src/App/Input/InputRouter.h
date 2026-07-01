#ifndef INPUTROUTER_H
#define INPUTROUTER_H

#include "Gui/Core/GuiTypes.h"

namespace cutum
{

class UApplication;

/// GLFW input routing (extracted from UApplication).
class UInputRouter
{
public:
  static GuiKeyEvent MakeGuiKeyEvent(int key, int action, int mods);

  bool RouteKey(UApplication &app, int key, int action, int mods);
  bool RouteChar(UApplication &app, unsigned int codepoint);
  bool RouteMouseButton(UApplication &app, int button, bool pressed, int x,
                        int y, int pointer_id);
  bool RouteMouseMove(UApplication &app, int x, int y, int pointer_id);
  bool RouteScroll(UApplication &app, double xoffset, double yoffset,
                   int mouse_x, int mouse_y);
};

} // namespace cutum

#endif
