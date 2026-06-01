#pragma once

#include "Gui/GuiScreenBase.h"
#include <memory>

namespace cutum {

class IGuiMenuHost;
class GuiWindow;
class GuiDialogFrame;
class WorldGenSettingsForm;

class NewWorldScreen : public GuiScreenBase {
public:
    explicit NewWorldScreen(IGuiMenuHost* host);

    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;

private:
    void Relayout();
    void OnCreate();

    IGuiMenuHost* host_{nullptr};
    GuiWindow* window_{nullptr};
    GuiDialogFrame* dialogFrame_{nullptr};
    std::unique_ptr<WorldGenSettingsForm> worldForm_;
};

} // namespace cutum
