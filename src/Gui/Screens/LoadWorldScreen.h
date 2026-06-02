#pragma once

#include "Gui/GuiScreenBase.h"

namespace cutum {

class IGuiMenuHost;
class GuiWindow;
class GuiDialogFrame;
class GuiListView;
class GuiLabel;
class GuiButton;

class LoadWorldScreen : public GuiScreenBase {
public:
    explicit LoadWorldScreen(IGuiMenuHost* host);

    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;

private:
    void Relayout();
    void OnLoad();

    IGuiMenuHost* host_{nullptr};
    GuiWindow* window_{nullptr};
    GuiDialogFrame* dialogFrame_{nullptr};
    GuiListView* list_{nullptr};
    GuiLabel* emptyLabel_{nullptr};
    GuiButton* loadBtn_{nullptr};
};

} // namespace cutum
