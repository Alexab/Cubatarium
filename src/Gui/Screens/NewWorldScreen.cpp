#include "NewWorldScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Layout/GuiLayout.h"
#include <algorithm>

namespace cutum {

namespace {

GuiGridSpec BuildWorldGridSpec(int width)
{
    GuiGridSpec spec;
    if (width < 680) {
        spec.columns = 1;
    } else {
        spec.columns = 2;
    }
    spec.hGap = 12;
    spec.vGap = 8;
    spec.padding = 4;
    spec.columnWeights = {1, 1};
    return spec;
}

} // namespace

UNewWorldScreen::UNewWorldScreen(IGuiMenuHost* host)
    : host_(host)
{
}

void UNewWorldScreen::OnCreate()
{
    if (!host_ || !worldForm_) {
        return;
    }
    const ProceduralSettings settings = worldForm_->ReadSettings();
    auto create = [this, settings]() { host_->CreateNewWorldWithSettings(settings); };
    host_->SaveIfNeededAndProceed(create);
}

void UNewWorldScreen::Build(UGuiContext& ctx)
{
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w > 0 && h > 0) {
        viewportW_ = w;
        viewportH_ = h;
    }

    const GuiTheme& theme = ctx.GetTheme();
    const ProceduralSettings procSnap = host_ ? host_->LoadProceduralTemplate() : ProceduralSettings{};

    auto backdrop = std::make_unique<UGuiPanel>(&theme);
    backdrop->SetBounds({0, 0, viewportW_, viewportH_});

    const int winW = std::min(820, viewportW_ - 32);
    const int winH = std::min(500, viewportH_ - 32);
    auto window = std::make_unique<UGuiWindow>(&theme, "New World");
    Window = window.get();
    window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

    auto frame = std::make_unique<UGuiDialogFrame>(&theme);
    dialogFrame_ = frame.get();
    frame->SetScrollbarMode(GuiScrollbarMode::Hidden);
    UGuiPanel& body = frame->AddScrollPage();
    worldPage_ = &body;
    worldForm_ = std::make_unique<UWorldGenSettingsForm>(&theme);
    worldForm_->SetSettings(procSnap);
    worldForm_->BuildInto(body);
    frame->SetScrollPageLayout(
        0,
        [this](const GuiRect& area) { return MeasureWorldPageHeight(area); },
        [this](const GuiRect& area) { LayoutWorldPage(area); });

    frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Create"))
        .SetOnClick([this]() { OnCreate(); });
    frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Cancel"))
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

void UNewWorldScreen::OnViewportChanged(int width, int height)
{
    UGuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void UNewWorldScreen::Relayout()
{
    if (!Window || !dialogFrame_) {
        return;
    }
    const int winW = std::min(820, viewportW_ - 32);
    const int winH = std::min(500, viewportH_ - 32);
    Window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});
    dialogFrame_->SetBounds(Window->GetClientArea());
    dialogFrame_->LayoutFrame();
}

int UNewWorldScreen::MeasureWorldPageHeight(const GuiRect& area) const
{
    if (!worldForm_) {
        return 0;
    }
    const GuiGridSpec spec = BuildWorldGridSpec(area.w);
    return worldForm_->MeasureGridHeight(area, spec);
}

void UNewWorldScreen::LayoutWorldPage(const GuiRect& area) const
{
    if (!worldForm_) {
        return;
    }
    const GuiGridSpec spec = BuildWorldGridSpec(area.w);
    worldForm_->LayoutGrid(area, spec);
}

} // namespace cutum
