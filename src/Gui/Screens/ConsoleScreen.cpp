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
    if (input_) {
        input_->SetFocused(visible);
    }
}

void ConsoleScreen::Toggle()
{
    SetVisible(!visible_);
}

void ConsoleScreen::Build(GuiContext& ctx)
{
    consoleTheme_ = ctx.GetTheme();
    consoleTheme_.panelBackground = {0.06f, 0.06f, 0.09f, 0.45f};
    consoleTheme_.panelBorder = {0.45f, 0.45f, 0.5f, 0.55f};
    consoleTheme_.buttonNormal = {0.18f, 0.18f, 0.2f, 0.5f};
    consoleTheme_.buttonHover = {0.28f, 0.28f, 0.31f, 0.55f};

    auto panel = std::make_unique<GuiPanel>(&consoleTheme_);
    panel->SetVisible(false);

    auto log = std::make_unique<GuiListView>(&consoleTheme_);
    log->SetAcceptKeyNavigation(false);
    logView_ = log.get();

    auto input = std::make_unique<GuiTextInput>(&consoleTheme_);
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
    const int panelY = viewportH_ - panelH;
    root_->SetBounds({0, panelY, viewportW_, panelH});
    if (logView_) {
        logView_->SetBounds({8, panelY + 8, viewportW_ - 16, panelH - 48});
    }
    if (input_) {
        input_->SetBounds({8, panelY + panelH - 40, viewportW_ - 16, 32});
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

bool ConsoleScreen::RouteKey(const GuiKeyEvent& event)
{
    if (!visible_ || !input_) {
        return false;
    }
    input_->SetFocused(true);
    return input_->OnKey(event);
}

bool ConsoleScreen::RouteChar(const GuiCharEvent& event)
{
    if (!visible_ || !input_) {
        return false;
    }
    input_->SetFocused(true);
    return input_->OnChar(event);
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
