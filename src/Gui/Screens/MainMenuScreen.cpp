#include "MainMenuScreen.h"
#include "Gui/GuiContext.h"
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
    auto panel = std::make_unique<GuiPanel>(&theme);
    panel->SetBounds({0, 0, 1280, 720});

    auto title = std::make_unique<GuiLabel>(&theme, "Cubatarium");
    title->SetBounds({0, 120, 1280, 40});

    auto play = std::make_unique<GuiButton>(&theme, "Play");
    play->SetBounds({490, 280, 300, 40});
    play->SetOnClick([this]() {
        if (actions_) {
            actions_->StartGame();
        }
    });

    auto settings = std::make_unique<GuiButton>(&theme, "Settings");
    settings->SetBounds({490, 340, 300, 40});
    settings->SetOnClick([this]() {
        if (actions_) {
            actions_->OpenSettings();
        }
    });

    auto quit = std::make_unique<GuiButton>(&theme, "Quit");
    quit->SetBounds({490, 400, 300, 40});
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
