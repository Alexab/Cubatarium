#include "ConsoleScreen.h"
#include "Game/GameSession.h"
#include "Gui/GuiContext.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"

#include <GLFW/glfw3.h>

namespace cutum {

ConsoleScreen::ConsoleScreen(GameSession* session)
    : session_(session)
{
}

void ConsoleScreen::SetVisible(bool visible)
{
    visible_ = visible;
    if (root_) {
        root_->SetVisible(visible);
    }
}

void ConsoleScreen::Toggle()
{
    SetVisible(!visible_);
}

void ConsoleScreen::Build(GuiContext& ctx)
{
    const GuiTheme& theme = ctx.GetTheme();
    auto panel = std::make_unique<GuiPanel>(&theme);
    panel->SetVisible(false);

    auto log = std::make_unique<GuiListView>(&theme);
    logView_ = log.get();

    auto input = std::make_unique<GuiTextInput>(&theme);
    input_ = input.get();

    panel->AddChild(std::move(log));
    panel->AddChild(std::move(input));
    root_ = std::move(panel);
    Relayout();
}

void ConsoleScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void ConsoleScreen::Relayout()
{
    if (!root_) {
        return;
    }
    const int panelH = viewportH_ * 40 / 100;
    root_->SetBounds({0, viewportH_ - panelH, viewportW_, panelH});
    if (logView_) {
        logView_->SetBounds({8, 8, viewportW_ - 16, panelH - 48});
    }
    if (input_) {
        input_->SetBounds({8, panelH - 40, viewportW_ - 16, 32});
    }
}

void ConsoleScreen::Update(double /*dt*/)
{
    if (!root_ || !visible_) {
        return;
    }
    if (session_ && logView_) {
        logView_->SetItems(session_->GetChatLog());
    }
}

void ConsoleScreen::SubmitCommand()
{
    if (!session_ || !input_) {
        return;
    }
    const std::string line = input_->GetText();
    if (line.empty()) {
        return;
    }
    session_->AddChatLine("> " + line);
    const auto result = session_->GetCommandRegistry().ExecuteLine(line);
    session_->AddChatLine(result.text);
    input_->SetText("");
}

} // namespace cutum
