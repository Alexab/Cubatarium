#include "MainMenuScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWidget.h"

namespace cutum {

MainMenuScreen::MainMenuScreen(IGuiGameActions* actions)
    : actions_(actions)
{
}

void MainMenuScreen::Build(GuiContext& ctx)
{
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w > 0 && h > 0) {
        viewportW_ = w;
        viewportH_ = h;
    }
    const GuiTheme& theme = ctx.GetTheme();
    auto panel = std::make_unique<GuiPanel>(&theme);
    panel->SetBounds({0, 0, viewportW_, viewportH_});

    auto title = std::make_unique<GuiLabel>(&theme, "Cubatarium");
    title->SetTextAlign(GuiTextAlign::Center);
    title->SetBounds({0, 0, 400, 56});
    title_ = title.get();

    const bool resume = actions_ && actions_->HasPausedSession();
    auto play = std::make_unique<GuiButton>(&theme, resume ? "Resume" : "Play");
    play->SetOnClick([this]() {
        if (actions_) {
            actions_->StartGame();
        }
    });

    auto settings = std::make_unique<GuiButton>(&theme, "Settings");
    settings->SetOnClick([this]() {
        if (actions_) {
            actions_->OpenSettings();
        }
    });

    auto quit = std::make_unique<GuiButton>(&theme, "Quit");
    quit->SetOnClick([this]() {
        if (actions_) {
            actions_->QuitApplication();
        }
    });

    buttons_.clear();
    buttons_.push_back(play.get());
    buttons_.push_back(settings.get());
    buttons_.push_back(quit.get());

    panel->AddChild(std::move(title));
    panel->AddChild(std::move(play));
    panel->AddChild(std::move(settings));
    panel->AddChild(std::move(quit));
    root_ = std::move(panel);
    Relayout();
}

void MainMenuScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void MainMenuScreen::Relayout()
{
    if (!root_) {
        return;
    }
    const GuiRect full{0, 0, viewportW_, viewportH_};
    root_->SetBounds(full);

    if (title_) {
        GuiLayout::AnchorChild(full, GuiAnchorKind::TopCenter, 20, title_);
    }

    if (!buttons_.empty()) {
        const int btnW = 300;
        const int btnH = 44;
        const int spacing = 12;
        const int stackH = static_cast<int>(buttons_.size()) * btnH +
                           static_cast<int>(buttons_.size() - 1) * spacing;
        GuiRect stackArea{(viewportW_ - btnW) / 2, (viewportH_ - stackH) / 2, btnW, stackH};
        std::vector<GuiWidget*> children;
        for (GuiButton* btn : buttons_) {
            children.push_back(btn);
        }
        GuiLayout::StackVertical(stackArea, spacing, 0, children);
    }
}

} // namespace cutum
