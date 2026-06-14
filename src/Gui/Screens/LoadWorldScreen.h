#pragma once

#include "Gui/GuiScreenBase.h"

namespace cutum {

class IGuiMenuHost;
class UGuiWindow;
class UGuiDialogFrame;
class UGuiListView;
class UGuiLabel;
class UGuiButton;

class ULoadWorldScreen : public UGuiScreenBase {
public:
    explicit ULoadWorldScreen(IGuiMenuHost* host);

    void Build(UGuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;

private:
    void Relayout();
    void OnLoad();

    IGuiMenuHost* host_{nullptr};
    UGuiWindow* Window{nullptr};
    UGuiDialogFrame* dialogFrame_{nullptr};
    UGuiListView* list_{nullptr};
    UGuiLabel* emptyLabel_{nullptr};
    UGuiButton* loadBtn_{nullptr};
};

} // namespace cutum
