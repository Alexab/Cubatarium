#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Widgets/GuiWidget.h"

#include <algorithm>

namespace cutum
{

UGuiScreenBase::~UGuiScreenBase() = default;

void UGuiScreenBase::OnAttach(UGuiContext & /*ctx*/) {}
void UGuiScreenBase::OnDetach() {}
void UGuiScreenBase::Update(double /*dt*/) {}

void UGuiScreenBase::SetViewportInsets(int insetLeft, int insetTop,
                                       int insetRight, int insetBottom)
{
  ContentOffsetX = std::max(0, insetLeft);
  ContentOffsetY = std::max(0, insetTop);
  ContentInsetRight = std::max(0, insetRight);
  ContentInsetBottom = std::max(0, insetBottom);
}

void UGuiScreenBase::OnViewportChanged(int width, int height)
{
  if (width > 0)
  {
    ViewportW = std::max(0, width - ContentOffsetX - ContentInsetRight);
  }
  if (height > 0)
  {
    ViewportH = std::max(0, height - ContentOffsetY - ContentInsetBottom);
  }
}

} // namespace cutum
