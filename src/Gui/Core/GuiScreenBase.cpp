#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiContext.h"

namespace cutum
{

void UGuiScreenBase::OnAttach(UGuiContext & /*ctx*/) {}
void UGuiScreenBase::OnDetach() {}
void UGuiScreenBase::Update(double /*dt*/) {}

void UGuiScreenBase::OnViewportChanged(int width, int height)
{
  if (width > 0)
  {
    viewportW_ = width;
  }
  if (height > 0)
  {
    viewportH_ = height;
  }
}

} // namespace cutum
