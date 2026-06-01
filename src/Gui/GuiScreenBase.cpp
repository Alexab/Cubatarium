#include "GuiScreenBase.h"
#include "GuiContext.h"

namespace cutum {

void GuiScreenBase::OnAttach(GuiContext& /*ctx*/) {}
void GuiScreenBase::OnDetach() {}
void GuiScreenBase::Update(double /*dt*/) {}

void GuiScreenBase::OnViewportChanged(int width, int height)
{
    if (width > 0) {
        viewportW_ = width;
    }
    if (height > 0) {
        viewportH_ = height;
    }
}

} // namespace cutum
