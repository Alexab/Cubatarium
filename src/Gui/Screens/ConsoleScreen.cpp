#include "ConsoleScreen.h"
#include "Game/GameSession.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPopupMenu.h"
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
    if (!visible) {
        historyBrowseFromEnd_ = -1;
        draftValid_ = false;
        if (popup_) {
            popup_->Close();
        }
    }
}

void ConsoleScreen::Toggle()
{
    SetVisible(!visible_);
}

void ConsoleScreen::AttachPopup(GuiPopupMenu* popup)
{
    popup_ = popup;
}

void ConsoleScreen::OnInputEdited()
{
    historyBrowseFromEnd_ = -1;
    draftValid_ = false;
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
    input->SetClipboard(ctx.GetClipboard());
    input->SetOnEdited([this]() { OnInputEdited(); });
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

bool ConsoleScreen::HandleHistoryNavigation(const GuiKeyEvent& event)
{
    if (!session_ || !input_) {
        return false;
    }
    if (event.action != GuiKeyAction::Press && event.action != GuiKeyAction::Repeat) {
        return false;
    }
    auto& history = session_->GetCommandHistory();
    if (event.keyCode == GLFW_KEY_UP) {
        if (historyBrowseFromEnd_ == -1) {
            draftLine_ = input_->GetText();
            draftValid_ = true;
        }
        const size_t next = static_cast<size_t>(historyBrowseFromEnd_ + 1);
        if (next < history.Size()) {
            historyBrowseFromEnd_ = static_cast<int>(next);
            input_->SetText(history.GetFromEnd(static_cast<size_t>(historyBrowseFromEnd_)));
            input_->ClearSelection();
        }
        return true;
    }
    if (event.keyCode == GLFW_KEY_DOWN) {
        if (historyBrowseFromEnd_ > 0) {
            --historyBrowseFromEnd_;
            input_->SetText(history.GetFromEnd(static_cast<size_t>(historyBrowseFromEnd_)));
            input_->ClearSelection();
        } else if (historyBrowseFromEnd_ == 0) {
            historyBrowseFromEnd_ = -1;
            input_->SetText(draftValid_ ? draftLine_ : "");
            input_->ClearSelection();
        }
        return true;
    }
    return false;
}

bool ConsoleScreen::RouteKey(const GuiKeyEvent& event)
{
    if (!visible_ || !input_) {
        return false;
    }
    input_->SetFocused(true);
    if (input_->HandleEditShortcut(event)) {
        return true;
    }
    if (HandleHistoryNavigation(event)) {
        return true;
    }
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

void ConsoleScreen::OpenContextMenu(int x, int y)
{
    if (!popup_ || !input_) {
        return;
    }
    popup_->SetItems({
        {"Copy", [this]() { input_->CopySelectionToClipboard(); }},
        {"Paste", [this]() { input_->PasteFromClipboard(); }},
        {"Cut", [this]() { input_->CutSelectionToClipboard(); }},
        {"Select all", [this]() { input_->SelectAll(); }},
    });
    popup_->OpenAt(x, y, viewportW_, viewportH_);
}

bool ConsoleScreen::IsPopupOpen() const
{
    return popup_ && popup_->IsOpen();
}

bool ConsoleScreen::RouteMouseButton(const GuiMouseEvent& event, GuiRenderer& renderer)
{
    if (!visible_) {
        return false;
    }
    if (popup_ && popup_->IsOpen()) {
        if (popup_->OnMouseDown(event)) {
            return true;
        }
    }
    if (event.pressed && event.button == GuiMouseButton::Right && input_ &&
        input_->GetBounds().Contains(event.x, event.y)) {
        input_->SetFocused(true);
        OpenContextMenu(event.x, event.y);
        return true;
    }
    if (input_ && input_->GetBounds().Contains(event.x, event.y)) {
        if (event.pressed && event.button == GuiMouseButton::Left) {
            input_->PointerDown(event, renderer);
            return true;
        }
        if (!event.pressed && event.button == GuiMouseButton::Left) {
            input_->OnMouseUp(event);
            return true;
        }
    }
    if (popup_ && popup_->IsOpen() && event.pressed) {
        popup_->Close();
        return true;
    }
    return false;
}

bool ConsoleScreen::RouteMouseMove(const GuiMouseEvent& event, GuiRenderer& renderer)
{
    if (!visible_ || !input_) {
        return false;
    }
    if (popup_ && popup_->IsOpen()) {
        if (popup_->OnMouseMove(event)) {
            return true;
        }
    }
    if (input_->PointerMove(event, renderer)) {
        return true;
    }
    return false;
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
    session_->GetCommandHistory().Append(line);
    session_->AddChatLine("> " + line);
    const auto result = session_->GetCommandRegistry().ExecuteLine(line);
    session_->AddChatLine(result.text);
    input_->SetText("");
    historyBrowseFromEnd_ = -1;
    draftValid_ = false;
}

} // namespace cutum
