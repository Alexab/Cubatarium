#include "MainMenuScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"

namespace cutum {

MainMenuScreen::MainMenuScreen(IGuiGameActions* actions)
    : actions_(actions)
{
}

void MainMenuScreen::Build(GuiContext& ctx)
{
    const GuiTheme& theme = ctx.GetTheme();
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w <= 0) {
        w = 1280;
    }
    if (h <= 0) {
        h = 720;
    }
    auto panel = std::make_unique<GuiPanel>(&theme);
    panel->SetBounds({0, 0, w, h});

    auto title = std::make_unique<GuiLabel>(&theme, "Cubatarium");
    title->SetBounds({0, h / 6, w, 48});

    const int btnW = 300;
    const int btnH = 44;
    const int btnX = (w - btnW) / 2;
    int btnY = h / 2 - 60;

    const bool resume = actions_ && actions_->HasPausedSession();
    auto play = std::make_unique<GuiButton>(&theme, resume ? "Resume" : "Play");
    play->SetBounds({btnX, btnY, btnW, btnH});
    play->SetOnClick([this]() {
        if (actions_) {
            actions_->StartGame();
        }
    });

    btnY += btnH + 12;
    auto settings = std::make_unique<GuiButton>(&theme, "Settings");
    settings->SetBounds({btnX, btnY, btnW, btnH});
    settings->SetOnClick([this]() {
        if (actions_) {
            actions_->OpenSettings();
        }
    });

    btnY += btnH + 12;
    auto quit = std::make_unique<GuiButton>(&theme, "Quit");
    quit->SetBounds({btnX, btnY, btnW, btnH});
    quit->SetOnClick([this]() {
        if (actions_) {
            actions_->QuitApplication();
        }
    });

    panel->AddChild(std::move(title));
    panel->AddChild(std::move(play));
    panel->AddChild(std::move(settings));
    panel->AddChild(std::move(quit));
    root_ = std::move(panel);
}

} // namespace cutum
