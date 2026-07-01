#ifndef INPUTROUTER_H
#define INPUTROUTER_H

#include "Gui/Core/GuiTypes.h"

namespace cutum
{

/// GLFW key routing (extracted incrementally from UApplication).
class UInputRouter
{
public:
  static GuiKeyEvent MakeGuiKeyEvent(int key, int action, int mods);
  // TODO: extract RouteKey routing stages from UApplication
};

} // namespace cutum

#endif
