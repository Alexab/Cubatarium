#include "App/Input/InputRouter.h"

#include <GLFW/glfw3.h>

namespace cutum
{

GuiKeyEvent UInputRouter::MakeGuiKeyEvent(int key, int action, int mods)
{
  GuiKeyEvent event;
  event.KeyCode = key;
  event.Action = action == GLFW_REPEAT
                     ? GuiKeyAction::Repeat
                     : (action == GLFW_PRESS ? GuiKeyAction::Press
                                             : GuiKeyAction::Release);
  event.Mods = mods;
  return event;
}

} // namespace cutum
