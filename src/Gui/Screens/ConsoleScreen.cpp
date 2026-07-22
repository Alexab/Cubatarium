#include "Gui/Screens/ConsoleScreen.h"
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

UConsoleScreen::UConsoleScreen(UGameSession *session) : Session(session) {}

void UConsoleScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (Input)
  {
    Input->SetFocused(visible);
#ifdef __ANDROID__
    if (visible)
    {
      AndroidSoftKeyboardSetTarget(Input);
    }
    else
    {
      AndroidSoftKeyboardClearTarget();
    }
#endif
  }
  if (!visible)
  {
    HistoryBrowseFromEnd = -1;
    DraftValid = false;
    if (Popup)
    {
      Popup->Close();
    }
  }
}

void UConsoleScreen::SetKeyboardInsetBottom(int bottom)
{
  const int inset = std::max(0, bottom);
  if (KeyboardInsetBottom == inset)
  {
    return;
  }
  KeyboardInsetBottom = inset;
  Relayout();
}

void UConsoleScreen::Toggle() { SetVisible(!Visible); }

void UConsoleScreen::AttachPopup(UGuiPopupMenu *popup) { Popup = popup; }

void UConsoleScreen::OnInputEdited()
{
  HistoryBrowseFromEnd = -1;
  DraftValid = false;
}

void UConsoleScreen::Build(UGuiContext &ctx)
{
  ConsoleTheme = ctx.GetTheme();
  ConsoleTheme.PanelBackground = {0.06f, 0.06f, 0.09f, 0.45f};
  ConsoleTheme.PanelBorder = {0.45f, 0.45f, 0.5f, 0.55f};
  ConsoleTheme.ButtonNormal = {0.18f, 0.18f, 0.2f, 0.5f};
  ConsoleTheme.ButtonHover = {0.28f, 0.28f, 0.31f, 0.55f};

  auto panel = std::make_unique<UGuiPanel>(&ConsoleTheme);
  panel->SetVisible(false);

  auto log = std::make_unique<UGuiListView>(&ConsoleTheme);
  log->SetAcceptKeyNavigation(false);
  LogView = log.get();

  auto input = std::make_unique<UGuiTextInput>(&ConsoleTheme);
  input->SetClipboard(ctx.GetClipboard());
  input->SetOnEdited([this]() { OnInputEdited(); });
  Input = input.get();

  panel->AddChild(std::move(log));
  panel->AddChild(std::move(input));
  Root = std::move(panel);
  Relayout();
}

void UConsoleScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UConsoleScreen::Relayout()
{
  if (!Root)
  {
    return;
  }
  const int ox = GetContentOffsetX();
  const int oy = GetContentOffsetY();
  const GuiTheme &theme =
      GetContext() ? GetContext()->GetTheme() : ConsoleTheme;
  const int padding = theme.Padding;
  const int inputH = std::max(Scaled(48), Scaled(40));

  if (KeyboardInsetBottom > 0)
  {
    const int inputY = oy + ViewportH - KeyboardInsetBottom - padding - inputH;
    const int logH = std::max(Scaled(72), inputH + Scaled(24));
    const int logY = inputY - 4 - logH;
    const int panelY = std::max(oy, logY - padding);
    const int panelH = inputY + inputH + padding - panelY;
    Root->SetBounds({ox, panelY, ViewportW, panelH});
    if (LogView)
    {
      LogView->SetBounds({ox + padding, logY, ViewportW - 2 * padding, logH});
      LogView->ScrollToEnd();
    }
    if (Input)
    {
      Input->SetBounds({ox + padding, inputY, ViewportW - 2 * padding, inputH});
    }
    return;
  }

  const int panelH = std::max(inputH + 96, ViewportH * 42 / 100);
  const int panelY = oy + std::max(0, ViewportH - panelH);
  Root->SetBounds({ox, panelY, ViewportW, panelH});
  if (LogView)
  {
    LogView->SetBounds({ox + padding, panelY + padding, ViewportW - 2 * padding,
                        std::max(32, panelH - inputH - 20)});
  }
  if (Input)
  {
    Input->SetBounds({ox + padding, panelY + panelH - inputH - padding,
                      ViewportW - 2 * padding, inputH});
  }
}

void UConsoleScreen::Update(double /*dt*/)
{
  if (!Root || !Visible)
  {
    return;
  }
  if (Session && LogView)
  {
    LogView->SetItems(Session->GetChatLog());
    if (KeyboardInsetBottom > 0)
    {
      LogView->ScrollToEnd();
    }
  }
}

bool UConsoleScreen::HandleHistoryNavigation(const GuiKeyEvent &event)
{
  if (!Session || !Input)
  {
    return false;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }
  auto &history = Session->GetCommandHistory();
  if (event.KeyCode == GuiKey::Up)
  {
    if (HistoryBrowseFromEnd == -1)
    {
      DraftLine = Input->GetText();
      DraftValid = true;
    }
    const size_t next = static_cast<size_t>(HistoryBrowseFromEnd + 1);
    if (next < history.Size())
    {
      HistoryBrowseFromEnd = static_cast<int>(next);
      Input->SetText(
          history.GetFromEnd(static_cast<size_t>(HistoryBrowseFromEnd)));
      Input->ClearSelection();
    }
    return true;
  }
  if (event.KeyCode == GuiKey::Down)
  {
    if (HistoryBrowseFromEnd > 0)
    {
      --HistoryBrowseFromEnd;
      Input->SetText(
          history.GetFromEnd(static_cast<size_t>(HistoryBrowseFromEnd)));
      Input->ClearSelection();
    }
    else if (HistoryBrowseFromEnd == 0)
    {
      HistoryBrowseFromEnd = -1;
      Input->SetText(DraftValid ? DraftLine : "");
      Input->ClearSelection();
    }
    return true;
  }
  return false;
}

bool UConsoleScreen::RouteKey(const GuiKeyEvent &event)
{
  if (!Visible || !Input)
  {
    return false;
  }
  Input->SetFocused(true);
  if (Input->HandleEditShortcut(event))
  {
    return true;
  }
  if (HandleHistoryNavigation(event))
  {
    return true;
  }
  return Input->OnKey(event);
}

bool UConsoleScreen::RouteChar(const GuiCharEvent &event)
{
  if (!Visible || !Input)
  {
    return false;
  }
  Input->SetFocused(true);
  return Input->OnChar(event);
}

void UConsoleScreen::OpenContextMenu(int x, int y)
{
  if (!Popup || !Input)
  {
    return;
  }
  Popup->SetItems({
      {"Copy", [this]() { Input->CopySelectionToClipboard(); }},
      {"Paste", [this]() { Input->PasteFromClipboard(); }},
      {"Cut", [this]() { Input->CutSelectionToClipboard(); }},
      {"Select all", [this]() { Input->SelectAll(); }},
  });
  Popup->OpenAt(x, y, ViewportW, ViewportH);
}

bool UConsoleScreen::IsPopupOpen() const { return Popup && Popup->IsOpen(); }

bool UConsoleScreen::RouteMouseButton(const GuiMouseEvent &event,
                                      UGuiRenderer &renderer)
{
  if (!Visible)
  {
    return false;
  }
  if (Popup && Popup->IsOpen())
  {
    if (Popup->OnMouseDown(event))
    {
      return true;
    }
  }
  if (event.Pressed && event.Button == GuiMouseButton::Right && Input &&
      Input->GetBounds().Contains(event.X, event.Y))
  {
    Input->SetFocused(true);
    OpenContextMenu(event.X, event.Y);
    return true;
  }
  if (Input && Input->GetBounds().Contains(event.X, event.Y))
  {
    if (event.Pressed && event.Button == GuiMouseButton::Left)
    {
      Input->PointerDown(event, renderer);
      return true;
    }
    if (!event.Pressed && event.Button == GuiMouseButton::Left)
    {
      Input->OnMouseUp(event);
      return true;
    }
  }
  if (Popup && Popup->IsOpen() && event.Pressed)
  {
    Popup->Close();
    return true;
  }
  return false;
}

bool UConsoleScreen::RouteMouseMove(const GuiMouseEvent &event,
                                    UGuiRenderer &renderer)
{
  if (!Visible || !Input)
  {
    return false;
  }
  if (Popup && Popup->IsOpen())
  {
    if (Popup->OnMouseMove(event))
    {
      return true;
    }
  }
  if (Input->PointerMove(event, renderer))
  {
    return true;
  }
  return false;
}

void UConsoleScreen::SubmitCommand()
{
  if (!Session || !Input)
  {
    return;
  }
  const std::string line = Input->GetText();
  if (line.empty())
  {
    return;
  }
  Session->GetCommandHistory().Append(line);
  Session->AddChatLine("> " + line);
  const auto result = Session->GetCommandRegistry().ExecuteLine(line);
  Session->AddChatLine(result.text);
  Input->SetText("");
  HistoryBrowseFromEnd = -1;
  DraftValid = false;
}

} // namespace cutum
