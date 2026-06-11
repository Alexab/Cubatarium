#include "MainMenuScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Version.h"

namespace cutum {

namespace {
constexpr int kQuitModalZOrder = 100;
}

MainMenuScreen::MainMenuScreen(IGuiGameActions* actions)
    : actions_(actions)
{
}

void MainMenuScreen::ShowQuitConfirmation(bool visible)
{
    quitDialogVisible_ = visible;
    if (quitBackdrop_) {
        quitBackdrop_->SetVisible(visible);
    }
    if (quitDialog_) {
        quitDialog_->SetVisible(visible);
    }
    if (quitMessage_) {
        quitMessage_->SetVisible(visible);
    }
    if (quitYesButton_) {
        quitYesButton_->SetVisible(visible);
    }
    if (quitNoButton_) {
        quitNoButton_->SetVisible(visible);
    }
    if (visible) {
        if (quitBackdrop_) {
            quitBackdrop_->SetZOrder(kQuitModalZOrder);
        }
        for (GuiButton* btn : buttons_) {
            if (btn) {
                btn->SetEnabled(false);
            }
        }
        RelayoutQuitDialog();
    } else {
        if (quitBackdrop_) {
            quitBackdrop_->SetZOrder(0);
        }
        for (GuiButton* btn : buttons_) {
            if (btn) {
                btn->SetEnabled(true);
            }
        }
    }
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
    auto primary = std::make_unique<GuiButton>(&theme, resume ? "Resume" : "Load Last World");
    primary->SetOnClick([this, resume]() {
        if (!actions_) {
            return;
        }
        if (resume) {
            actions_->ResumeGame();
        } else {
            actions_->LoadLastWorld();
        }
    });

    auto loadWorld = std::make_unique<GuiButton>(&theme, "Load World");
    loadWorld->SetOnClick([this]() {
        if (actions_) {
            actions_->OpenLoadWorld();
        }
    });

    auto newWorld = std::make_unique<GuiButton>(&theme, "New World");
    newWorld->SetOnClick([this]() {
        if (actions_) {
            actions_->OpenNewWorld();
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
        ShowQuitConfirmation(true);
    });

    buttons_.clear();
    buttons_.push_back(primary.get());
    buttons_.push_back(loadWorld.get());
    buttons_.push_back(newWorld.get());
    buttons_.push_back(settings.get());
    buttons_.push_back(quit.get());

    auto version = std::make_unique<GuiLabel>(&theme, kCubatariumVersion);
    version->SetTextAlign(GuiTextAlign::Left);
    version->SetUseSecondaryColor(true);
    versionLabel_ = version.get();

    auto quitBackdrop = std::make_unique<GuiPanel>(&theme);
    quitBackdrop->SetVisible(false);
    quitBackdrop_ = quitBackdrop.get();

    auto quitDialog = std::make_unique<GuiPanel>(&theme);
    quitDialog->SetVisible(false);
    quitDialog_ = quitDialog.get();

    auto quitMessage = std::make_unique<GuiLabel>(&theme, "Exit the application?");
    quitMessage->SetTextAlign(GuiTextAlign::Center);
    quitMessage->SetVisible(false);
    quitMessage_ = quitMessage.get();

    auto quitYes = std::make_unique<GuiButton>(&theme, "Yes");
    quitYes->SetVisible(false);
    quitYes->SetOnClick([this]() {
        if (actions_) {
            actions_->QuitApplication();
        }
    });
    quitYesButton_ = quitYes.get();

    auto quitNo = std::make_unique<GuiButton>(&theme, "No");
    quitNo->SetVisible(false);
    quitNo->SetOnClick([this]() { ShowQuitConfirmation(false); });
    quitNoButton_ = quitNo.get();

    quitDialog->AddChild(std::move(quitMessage));
    quitDialog->AddChild(std::move(quitYes));
    quitDialog->AddChild(std::move(quitNo));
    quitBackdrop->AddChild(std::move(quitDialog));

    panel->AddChild(std::move(title));
    panel->AddChild(std::move(version));
    panel->AddChild(std::move(primary));
    panel->AddChild(std::move(loadWorld));
    panel->AddChild(std::move(newWorld));
    panel->AddChild(std::move(settings));
    panel->AddChild(std::move(quit));
    panel->AddChild(std::move(quitBackdrop));
    root_ = std::move(panel);
    Relayout();
}

void MainMenuScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void MainMenuScreen::RelayoutQuitDialog()
{
    if (!quitBackdrop_ || !quitDialog_ || !quitMessage_ || !quitYesButton_ || !quitNoButton_) {
        return;
    }
    quitBackdrop_->SetBounds({0, 0, viewportW_, viewportH_});

    constexpr int dialogW = 360;
    constexpr int dialogH = 160;
    const int dialogX = (viewportW_ - dialogW) / 2;
    const int dialogY = (viewportH_ - dialogH) / 2;
    quitDialog_->SetBounds({dialogX, dialogY, dialogW, dialogH});

    quitMessage_->SetBounds({dialogX + 16, dialogY + 16, dialogW - 32, 40});

    constexpr int btnW = 120;
    constexpr int btnH = 40;
    constexpr int btnGap = 16;
    const int btnY = dialogY + dialogH - btnH - 20;
    const int totalBtnW = btnW * 2 + btnGap;
    const int btnStartX = dialogX + (dialogW - totalBtnW) / 2;
    quitYesButton_->SetBounds({btnStartX, btnY, btnW, btnH});
    quitNoButton_->SetBounds({btnStartX + btnW + btnGap, btnY, btnW, btnH});
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

    if (versionLabel_) {
        constexpr int margin = 8;
        constexpr int labelH = 24;
        constexpr int labelW = 360;
        versionLabel_->SetBounds(
            {margin, viewportH_ - labelH - margin, labelW, labelH});
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

    if (quitDialogVisible_) {
        RelayoutQuitDialog();
    }
}

} // namespace cutum
