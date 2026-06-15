#include "ConsoleScreen.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiTextInput.h"

#include "Gui/Core/GuiKeyCodes.h"
#ifdef __ANDROID__
#include "android_soft_keyboard.h"
#endif

namespace cutum
{

UConsoleScreen::UConsoleScreen(UGameSession *session) : session_(session) {}

void UConsoleScreen::SetVisible(bool visible)
{
  visible_ = visible;
  if (root_)
  {
    root_->SetVisible(visible);
  }
  if (input_)
  {
    input_->SetFocused(visible);
#ifdef __ANDROID__
    if (visible)
    {
      AndroidSoftKeyboardSetTarget(input_);
    }
    else
    {
      AndroidSoftKeyboardClearTarget();
    }
#endif
  }
  if (!visible)
  {
    historyBrowseFromEnd_ = -1;
    draftValid_ = false;
    if (popup_)
    {
      popup_->Close();
    }
  }
}

void UConsoleScreen::SetKeyboardInsetBottom(int bottom)
{
  const int inset = std::max(0, bottom);
  if (keyboardInsetBottom_ == inset)
  {
    return;
  }
  keyboardInsetBottom_ = inset;
  Relayout();
}

void UConsoleScreen::Toggle() { SetVisible(!visible_); }

void UConsoleScreen::AttachPopup(UGuiPopupMenu *popup) { popup_ = popup; }

void UConsoleScreen::OnInputEdited()
{
  historyBrowseFromEnd_ = -1;
  draftValid_ = false;
}

void UConsoleScreen::Build(UGuiContext &ctx)
{
  consoleTheme_ = ctx.GetTheme();
  consoleTheme_.panelBackground = {0.06f, 0.06f, 0.09f, 0.45f};
  consoleTheme_.panelBorder = {0.45f, 0.45f, 0.5f, 0.55f};
  consoleTheme_.buttonNormal = {0.18f, 0.18f, 0.2f, 0.5f};
  consoleTheme_.buttonHover = {0.28f, 0.28f, 0.31f, 0.55f};

  auto panel = std::make_unique<UGuiPanel>(&consoleTheme_);
  panel->SetVisible(false);

  auto log = std::make_unique<UGuiListView>(&consoleTheme_);
  log->SetAcceptKeyNavigation(false);
  logView_ = log.get();

  auto input = std::make_unique<UGuiTextInput>(&consoleTheme_);
  input->SetClipboard(ctx.GetClipboard());
  input->SetOnEdited([this]() { OnInputEdited(); });
  input_ = input.get();

  panel->AddChild(std::move(log));
  panel->AddChild(std::move(input));
  root_ = std::move(panel);
  Relayout();
}

void UConsoleScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UConsoleScreen::Relayout()
{
  if (!root_)
  {
    return;
  }
  const int ox = GetContentOffsetX();
  const int oy = GetContentOffsetY();
  constexpr int padding = 8;
  const int inputH = std::max(48, 40);

  if (keyboardInsetBottom_ > 0)
  {
    const int inputY =
        oy + viewportH_ - keyboardInsetBottom_ - padding - inputH;
    const int logH = std::max(72, inputH + 24);
    const int logY = inputY - 4 - logH;
    const int panelY = std::max(oy, logY - padding);
    const int panelH = inputY + inputH + padding - panelY;
    root_->SetBounds({ox, panelY, viewportW_, panelH});
    if (logView_)
    {
      logView_->SetBounds({ox + padding, logY, viewportW_ - 2 * padding, logH});
      logView_->ScrollToEnd();
    }
    if (input_)
    {
      input_->SetBounds({ox + padding, inputY, viewportW_ - 2 * padding, inputH});
    }
    return;
  }

  const int panelH = std::max(inputH + 96, viewportH_ * 42 / 100);
  const int panelY = oy + std::max(0, viewportH_ - panelH);
  root_->SetBounds({ox, panelY, viewportW_, panelH});
  if (logView_)
  {
    logView_->SetBounds({ox + padding, panelY + padding, viewportW_ - 2 * padding,
                         std::max(32, panelH - inputH - 20)});
  }
  if (input_)
  {
    input_->SetBounds({ox + padding, panelY + panelH - inputH - padding,
                       viewportW_ - 2 * padding, inputH});
  }
}

void UConsoleScreen::Update(double /*dt*/)
{
  if (!root_ || !visible_)
  {
    return;
  }
  if (session_ && logView_)
  {
    logView_->SetItems(session_->GetChatLog());
    if (keyboardInsetBottom_ > 0)
    {
      logView_->ScrollToEnd();
    }
  }
}

bool UConsoleScreen::HandleHistoryNavigation(const GuiKeyEvent &event)
{
  if (!session_ || !input_)
  {
    return false;
  }
  if (event.action != GuiKeyAction::Press &&
      event.action != GuiKeyAction::Repeat)
  {
    return false;
  }
  auto &history = session_->GetCommandHistory();
  if (event.keyCode == GuiKey::Up)
  {
    if (historyBrowseFromEnd_ == -1)
    {
      draftLine_ = input_->GetText();
      draftValid_ = true;
    }
    const size_t next = static_cast<size_t>(historyBrowseFromEnd_ + 1);
    if (next < history.Size())
    {
      historyBrowseFromEnd_ = static_cast<int>(next);
      input_->SetText(
          history.GetFromEnd(static_cast<size_t>(historyBrowseFromEnd_)));
      input_->ClearSelection();
    }
    return true;
  }
  if (event.keyCode == GuiKey::Down)
  {
    if (historyBrowseFromEnd_ > 0)
    {
      --historyBrowseFromEnd_;
      input_->SetText(
          history.GetFromEnd(static_cast<size_t>(historyBrowseFromEnd_)));
      input_->ClearSelection();
    }
    else if (historyBrowseFromEnd_ == 0)
    {
      historyBrowseFromEnd_ = -1;
      input_->SetText(draftValid_ ? draftLine_ : "");
      input_->ClearSelection();
    }
    return true;
  }
  return false;
}

bool UConsoleScreen::RouteKey(const GuiKeyEvent &event)
{
  if (!visible_ || !input_)
  {
    return false;
  }
  input_->SetFocused(true);
  if (input_->HandleEditShortcut(event))
  {
    return true;
  }
  if (HandleHistoryNavigation(event))
  {
    return true;
  }
  return input_->OnKey(event);
}

bool UConsoleScreen::RouteChar(const GuiCharEvent &event)
{
  if (!visible_ || !input_)
  {
    return false;
  }
  input_->SetFocused(true);
  return input_->OnChar(event);
}

void UConsoleScreen::OpenContextMenu(int x, int y)
{
  if (!popup_ || !input_)
  {
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

bool UConsoleScreen::IsPopupOpen() const { return popup_ && popup_->IsOpen(); }

bool UConsoleScreen::RouteMouseButton(const GuiMouseEvent &event,
                                      UGuiRenderer &renderer)
{
  if (!visible_)
  {
    return false;
  }
  if (popup_ && popup_->IsOpen())
  {
    if (popup_->OnMouseDown(event))
    {
      return true;
    }
  }
  if (event.pressed && event.button == GuiMouseButton::Right && input_ &&
      input_->GetBounds().Contains(event.x, event.y))
  {
    input_->SetFocused(true);
    OpenContextMenu(event.x, event.y);
    return true;
  }
  if (input_ && input_->GetBounds().Contains(event.x, event.y))
  {
    if (event.pressed && event.button == GuiMouseButton::Left)
    {
      input_->PointerDown(event, renderer);
#ifdef __ANDROID__
      AndroidSoftKeyboardSetTarget(input_);
#endif
      return true;
    }
    if (!event.pressed && event.button == GuiMouseButton::Left)
    {
      input_->OnMouseUp(event);
      return true;
    }
  }
  if (popup_ && popup_->IsOpen() && event.pressed)
  {
    popup_->Close();
    return true;
  }
  return false;
}

bool UConsoleScreen::RouteMouseMove(const GuiMouseEvent &event,
                                    UGuiRenderer &renderer)
{
  if (!visible_ || !input_)
  {
    return false;
  }
  if (popup_ && popup_->IsOpen())
  {
    if (popup_->OnMouseMove(event))
    {
      return true;
    }
  }
  if (input_->PointerMove(event, renderer))
  {
    return true;
  }
  return false;
}

void UConsoleScreen::SubmitCommand()
{
  if (!session_ || !input_)
  {
    return;
  }
  const std::string line = input_->GetText();
  if (line.empty())
  {
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
