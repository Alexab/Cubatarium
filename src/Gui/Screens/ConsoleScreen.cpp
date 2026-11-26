#include "Gui/Screens/ConsoleScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPopupMenu.h"
#include "Gui/Widgets/GuiTextInput.h"

#include "Gui/Core/GuiKeyCodes.h"
#include <typeinfo>
#ifdef __ANDROID__
#include "android_soft_keyboard.h"
#endif

namespace cutum
{

UConsoleScreen::UConsoleScreen(UGameSession *session) : Session(session) {}

struct UConsoleScreen::EditedAction
{
  UConsoleScreen *owner;
  void operator()() const { owner->OnInputEdited(); }
};

template<void (UGuiTextInput::*Edit)()> struct UConsoleScreen::PopupEditAction
{
  UConsoleScreen *owner;
  void operator()() const { (owner->Input->*Edit)(); }
};

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
  panel->SetVisible(Visible);

  auto log = std::make_unique<UGuiListView>(&ConsoleTheme);
  log->SetAcceptKeyNavigation(false);
  if (Session)
    log->BorrowItems(Session->GetChatLog());
  auto *log_view = log.get();

  auto input = std::make_unique<UGuiTextInput>(&ConsoleTheme);
  input->SetClipboard(ctx.GetClipboard());
  input->SetFocused(Visible);
  input->SetOnEdited(EditedAction{this});
  auto *text_input = input.get();

  panel->AddChild(std::move(log));
  panel->AddChild(std::move(input));
  Root = std::move(panel);
  LogView = log_view;
  Input = text_input;
  Relayout();
}

ConsoleStorage UConsoleScreen::InspectOwnedStorage(const UGuiContext &context,
                                                    const UGameSession *session,
                                                    const UGuiPopupMenu *popup) const noexcept
{
  ConsoleStorage out;
  out.present = true;
  if (typeid(*this) != typeid(UConsoleScreen) || Context != &context ||
      context.GetScreen() == this || !Session || Session != session || Popup != popup ||
      !Root || typeid(*Root) != typeid(UGuiPanel))
    return out;
  const auto &panel = static_cast<const UGuiPanel &>(*Root);
  const auto &children = panel.GetChildren();
  if (children.size() != 2 || !children[0] || !children[1] ||
      children[0].get() != LogView || children[1].get() != Input ||
      typeid(*children[0]) != typeid(UGuiListView) || typeid(*children[1]) != typeid(UGuiTextInput) ||
      !LogView->GetChildren().empty() || !Input->GetChildren().empty() ||
      panel.Theme != &ConsoleTheme || LogView->Theme != &ConsoleTheme || Input->Theme != &ConsoleTheme ||
      Input->Clipboard != context.GetClipboard() || panel.IsVisible() != Visible)
    return out;
  const auto &log = Session->GetChatLog();
  const auto &history = Session->GetCommandHistory();
  out.borrowed_log = LogView->BorrowedItems == &log && !LogView->Items.capacity();
  out.bindings_match = out.borrowed_log;
  if (!out.bindings_match)
    return out;

  StlSourceStorage child_slots, input_text, draft_text, log_slots, log_text, history_slots,
      history_text, history_path, popup_slots, popup_text, callbacks, total;
  for (const auto *widget : {Root.get(), children[0].get(), children[1].get()})
    child_slots.Buffer(widget->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  input_text.Text(Input->GetText());
  draft_text.Text(DraftLine);
  log_slots.Buffer(log.capacity(), sizeof(std::string));
  for (const auto &line : log)
    log_text.Text(line);
  history_slots.Buffer(history.Entries().capacity(), sizeof(std::string));
  for (const auto &line : history.Entries())
    history_text.Text(line);
  history_path.Text(history.GetFilePath().native());

  const auto edited = InspectKnownOwnerCallback<EditedAction>(
      Input->OnEdited, this, StlSourceStorage::SupportedProfile());
  bool known = edited.supported && edited.present && !LogView->OnSelectionChanged &&
               !LogView->AcceptKeyNavigation;
  callbacks.Add(edited.requests);
  if (Popup)
  {
    out.bindings_match = typeid(*Popup) == typeid(UGuiPopupMenu) &&
        Popup->Theme == &context.GetTheme() && Popup->GetChildren().empty() &&
        (Popup->Items.empty() || Popup->Items.size() == 4) &&
        Popup->IsOpen() == Popup->IsVisible();
    if (!out.bindings_match)
      return out;
    out.popup_bytes = sizeof(UGuiPopupMenu);
    popup_slots.Buffer(Popup->Items.capacity(), sizeof(GuiPopupMenuItem));
    popup_slots.Buffer(Popup->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    for (const auto &item : Popup->Items)
      popup_text.Text(item.label);
    if (!Popup->Items.empty())
    {
      const auto action = [&](auto sample, size_t index) {
        const auto source = InspectKnownOwnerCallback<decltype(sample)>(
            Popup->Items[index].Action, this, StlSourceStorage::SupportedProfile());
        callbacks.Add(source.requests);
        return source.supported && source.present;
      };
      known &= action(PopupEditAction<&UGuiTextInput::CopySelectionToClipboard>{nullptr}, 0);
      known &= action(PopupEditAction<&UGuiTextInput::PasteFromClipboard>{nullptr}, 1);
      known &= action(PopupEditAction<&UGuiTextInput::CutSelectionToClipboard>{nullptr}, 2);
      known &= action(PopupEditAction<&UGuiTextInput::SelectAll>{nullptr}, 3);
    }
    out.popup_entries = Popup->Items.size();
    out.popup_open = Popup->IsOpen();
  }
  out.visible = Visible;
  out.screen_bytes = sizeof(*this);
  out.widget_bytes = sizeof(UGuiPanel) + sizeof(UGuiListView) + sizeof(UGuiTextInput);
  out.children_bytes = child_slots.bytes.requested;
  out.input_text = input_text.bytes.requested;
  out.draft_text = draft_text.bytes.requested;
  out.log_rows = log.size();
  out.log_slots = log_slots.bytes.requested;
  out.log_text = log_text.bytes.requested;
  out.history_rows = history.Size();
  out.history_slots = history_slots.bytes.requested;
  out.history_text = history_text.bytes.requested;
  out.history_path = history_path.bytes.requested;
  out.popup_slots = popup_slots.bytes.requested;
  out.popup_text = popup_text.bytes.requested;
  out.callback_bytes = callbacks.bytes.requested;
  for (const auto *part : {&child_slots, &input_text, &draft_text, &log_slots, &log_text,
                           &history_slots, &history_text, &history_path, &popup_slots,
                           &popup_text, &callbacks})
  {
    total.Add(part->bytes.requested);
    known &= part->bytes.supported;
  }
  for (const auto bytes : {out.screen_bytes, out.widget_bytes, out.popup_bytes})
    total.Add(bytes);
  out.requested = total.bytes.requested;
  out.supported = known && total.bytes.supported;
  return out;
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
    LogView->RefreshItems();
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
      Input->SetText(history.Entries()[history.Size() - 1 - next]);
      HistoryBrowseFromEnd = static_cast<int>(next);
      Input->ClearSelection();
    }
    return true;
  }
  if (event.KeyCode == GuiKey::Down)
  {
    if (HistoryBrowseFromEnd > 0)
    {
      const auto next = static_cast<size_t>(HistoryBrowseFromEnd - 1);
      if (next < history.Size())
        Input->SetText(history.Entries()[history.Size() - 1 - next]);
      else
        Input->SetText("");
      HistoryBrowseFromEnd = static_cast<int>(next);
      Input->ClearSelection();
    }
    else if (HistoryBrowseFromEnd == 0)
    {
      if (DraftValid)
        Input->SetText(DraftLine);
      else
        Input->SetText("");
      HistoryBrowseFromEnd = -1;
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
      {"Copy", PopupEditAction<&UGuiTextInput::CopySelectionToClipboard>{this}},
      {"Paste", PopupEditAction<&UGuiTextInput::PasteFromClipboard>{this}},
      {"Cut", PopupEditAction<&UGuiTextInput::CutSelectionToClipboard>{this}},
      {"Select all", PopupEditAction<&UGuiTextInput::SelectAll>{this}},
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
