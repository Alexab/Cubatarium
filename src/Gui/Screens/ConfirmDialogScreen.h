#pragma once

#include "Gui/GuiScreenBase.h"
#include <functional>
#include <string>

namespace cutum {

class IGuiMenuHost;

class ConfirmDialogScreen : public GuiScreenBase {
public:
    ConfirmDialogScreen(IGuiMenuHost* host, std::string message, std::function<void()> onYes,
                        std::function<void()> onNo);

    void Build(GuiContext& ctx) override;

private:
    IGuiMenuHost* host_{nullptr};
    std::string message_;
    std::function<void()> onYes_;
    std::function<void()> onNo_;
};

} // namespace cutum
