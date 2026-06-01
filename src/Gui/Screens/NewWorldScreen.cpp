#include "NewWorldScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include <algorithm>

namespace cutum {

NewWorldScreen::NewWorldScreen(IGuiMenuHost* host)
    : host_(host)
{
}

void NewWorldScreen::OnCreate()
{
    if (!host_ || !worldForm_) {
        return;
    }
    const ProceduralSettings settings = worldForm_->ReadSettings();
    auto create = [this, settings]() { host_->CreateNewWorldWithSettings(settings); };
    host_->RequestConfirmSaveAndProceed("Save the current world and create a new one?", create);
}

void NewWorldScreen::Build(GuiContext& ctx)
{
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w > 0 && h > 0) {
        viewportW_ = w;
        viewportH_ = h;
    }

    const GuiTheme& theme = ctx.GetTheme();
    const ProceduralSettings procSnap = host_ ? host_->LoadProceduralTemplate() : ProceduralSettings{};

    auto backdrop = std::make_unique<GuiPanel>(&theme);
    backdrop->SetBounds({0, 0, viewportW_, viewportH_});

    const int winW = std::min(520, viewportW_ - 40);
    const int winH = std::min(520, viewportH_ - 40);
    auto window = std::make_unique<GuiWindow>(&theme, "New World");
    window_ = window.get();
    window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

    auto frame = std::make_unique<GuiDialogFrame>(&theme);
    dialogFrame_ = frame.get();
    GuiPanel& body = frame->AddScrollPage();
    worldForm_ = std::make_unique<WorldGenSettingsForm>(&theme);
    worldForm_->SetSettings(procSnap);
    worldForm_->BuildInto(body);

    frame->AddFooterButton(std::make_unique<GuiButton>(&theme, "Create"))
        .SetOnClick([this]() { OnCreate(); });
    frame->AddFooterButton(std::make_unique<GuiButton>(&theme, "Cancel"))
        .SetOnClick([this]() {
            if (host_) {
                host_->ReturnToMainMenu();
            }
        });

    window->AddChild(std::move(frame));
    backdrop->AddChild(std::move(window));
    root_ = std::move(backdrop);
    Relayout();
}

void NewWorldScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void NewWorldScreen::Relayout()
{
    if (!window_ || !dialogFrame_) {
        return;
    }
    const int winW = std::min(520, viewportW_ - 40);
    const int winH = std::min(520, viewportH_ - 40);
    window_->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});
    dialogFrame_->SetBounds(window_->GetClientArea());
    dialogFrame_->LayoutFrame();
}

} // namespace cutum
