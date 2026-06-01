#include "ConfirmDialogScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"

namespace cutum {

ConfirmDialogScreen::ConfirmDialogScreen(IGuiMenuHost* host, std::string message,
                                         std::function<void()> onYes, std::function<void()> onNo)
    : host_(host)
    , message_(std::move(message))
    , onYes_(std::move(onYes))
    , onNo_(std::move(onNo))
{
}

void ConfirmDialogScreen::Build(GuiContext& ctx)
{
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w > 0 && h > 0) {
        viewportW_ = w;
        viewportH_ = h;
    }

    const GuiTheme& theme = ctx.GetTheme();
    auto backdrop = std::make_unique<GuiPanel>(&theme);
    backdrop->SetBounds({0, 0, viewportW_, viewportH_});

    const int winW = 420;
    const int winH = 180;
    auto window = std::make_unique<GuiWindow>(&theme, "Confirm");
    window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

    GuiRect client = window->GetClientArea();
    auto msg = std::make_unique<GuiLabel>(&theme, message_);
    msg->SetBounds({client.x + 8, client.y + 8, client.w - 16, 48});

    auto yesBtn = std::make_unique<GuiButton>(&theme, "Yes");
    auto noBtn = std::make_unique<GuiButton>(&theme, "No");
    yesBtn->SetOnClick([this]() {
        if (onYes_) {
            onYes_();
        }
    });
    noBtn->SetOnClick([this]() {
        if (onNo_) {
            onNo_();
        } else if (host_) {
            host_->ReturnToMainMenu();
        }
    });

    GuiRect btnArea{client.x, client.y + client.h - 44, client.w, 40};
    std::vector<GuiWidget*> btns{yesBtn.get(), noBtn.get()};
    GuiLayout::StackHorizontal(btnArea, 12, 0, btns);

    window->AddChild(std::move(msg));
    window->AddChild(std::move(yesBtn));
    window->AddChild(std::move(noBtn));
    backdrop->AddChild(std::move(window));
    root_ = std::move(backdrop);
}

} // namespace cutum
