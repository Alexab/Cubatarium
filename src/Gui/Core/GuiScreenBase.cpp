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
  contentOffsetX_ = std::max(0, insetLeft);
  contentOffsetY_ = std::max(0, insetTop);
  contentInsetRight_ = std::max(0, insetRight);
  contentInsetBottom_ = std::max(0, insetBottom);
}

void UGuiScreenBase::OnViewportChanged(int width, int height)
{
  if (width > 0)
  {
    viewportW_ =
        std::max(0, width - contentOffsetX_ - contentInsetRight_);
  }
  if (height > 0)
  {
    viewportH_ =
        std::max(0, height - contentOffsetY_ - contentInsetBottom_);
  }
}

} // namespace cutum
