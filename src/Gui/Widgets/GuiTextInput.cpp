#include "Gui/Widgets/GuiTextInput.h"
#include "Console/ConsoleInputSanitize.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#ifdef __ANDROID__
#include "android_soft_keyboard.h"
#endif

#include "Gui/Core/GuiKeyCodes.h"
#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>

namespace cutum
{

UGuiTextInput::UGuiTextInput(const GuiTheme *theme) : Theme(theme) {}

int UGuiTextInput::GetPreferredHeight() const
{
  return Theme ? Theme->FontSizeBody + Theme->Padding * 2 : 28;
}

int UGuiTextInput::TextPadding() const { return Theme ? Theme->Padding : 8; }

int UGuiTextInput::TextLeft() const { return Bounds.X + TextPadding(); }

GuiRect UGuiTextInput::TextClipRect() const
{
  return Bounds.Inset(TextPadding());
}

int UGuiTextInput::TextLineHeight(UGuiRenderer &renderer) const
{
  const int measured = renderer.MeasureTextHeight("Ag");
  if (measured > 0)
  {
    return measured;
  }
  return Theme ? Theme->FontSizeBody : 16;
}

int UGuiTextInput::TextTopY(UGuiRenderer &renderer) const
{
  const int lineH = TextLineHeight(renderer);
  return Bounds.Y + std::max(0, (Bounds.H - lineH) / 2);
}

bool UGuiTextInput::CanFocus() const { return Enabled && Visible; }

size_t UGuiTextInput::SelMin() const { return std::min(SelAnchor, SelEnd); }

size_t UGuiTextInput::SelMax() const { return std::max(SelAnchor, SelEnd); }

bool UGuiTextInput::HasSelection() const { return SelMin() < SelMax(); }

void UGuiTextInput::ClearSelection() { SelAnchor = SelEnd = CaretPos; }

void UGuiTextInput::NotifyEdited()
{
  if (!ProgrammaticChange && OnEdited)
  {
    OnEdited();
  }
}

void UGuiTextInput::SetText(const std::string &text)
{
  ProgrammaticChange = true;
  Buffer = SanitizeConsoleLine(text);
  if (Buffer.size() > kMaxLength)
  {
    Buffer.resize(kMaxLength);
  }
  CaretPos = Buffer.size();
  ClearSelection();
  ScrollX = 0;
  ProgrammaticChange = false;
}

void UGuiTextInput::DeleteSelection()
{
  if (!HasSelection())
  {
    return;
  }
  const size_t a = SelMin();
  const size_t b = SelMax();
  Buffer.erase(a, b - a);
  CaretPos = a;
  ClearSelection();
}

void UGuiTextInput::InsertText(const std::string &text)
{
  if (text.empty())
  {
    return;
  }
  DeleteSelection();
  const size_t room =
      kMaxLength > Buffer.size() ? kMaxLength - Buffer.size() : 0;
  std::string chunk = text.substr(0, room);
  Buffer.insert(CaretPos, chunk);
  CaretPos += chunk.size();
  NotifyEdited();
}

size_t UGuiTextInput::CaretIndexFromX(int mouseX, UGuiRenderer &renderer) const
{
  const int left = TextLeft() - ScrollX;
  const int relX = mouseX - left;
  if (relX <= 0)
  {
    return 0;
  }
  size_t lo = 0;
  size_t hi = Buffer.size();
  while (lo < hi)
  {
    const size_t mid = lo + (hi - lo) / 2;
    if (renderer.MeasureTextWidth(Buffer.substr(0, mid)) < relX)
    {
      lo = mid + 1;
    }
    else
    {
      hi = mid;
    }
  }
  return std::min(lo, Buffer.size());
}

void UGuiTextInput::EnsureCaretVisible(UGuiRenderer &renderer)
{
  const GuiRect clip = TextClipRect();
  if (clip.W <= 0)
  {
    return;
  }
  const int textWidth = renderer.MeasureTextWidth(Buffer);
  if (textWidth <= clip.W)
  {
    ScrollX = 0;
    return;
  }
  const int caretRel = renderer.MeasureTextWidth(Buffer.substr(0, CaretPos));
  const int selStartRel =
      HasSelection()
          ? renderer.MeasureTextWidth(Buffer.substr(0, SelMin()))
          : caretRel;
  const int selEndRel =
      HasSelection()
          ? renderer.MeasureTextWidth(Buffer.substr(0, SelMax()))
          : caretRel;
  const int focusStart = std::min(selStartRel, caretRel);
  const int focusEnd = std::max(selEndRel, caretRel);
  if (focusStart < ScrollX)
  {
    ScrollX = focusStart;
  }
  if (focusEnd > ScrollX + clip.W - 4)
  {
    ScrollX = std::max(0, focusEnd - clip.W + 4);
  }
  const int maxScroll = std::max(0, textWidth - clip.W);
  ScrollX = std::clamp(ScrollX, 0, maxScroll);
}

std::string UGuiTextInput::GetSelectedText() const
{
  if (!HasSelection())
  {
    return {};
  }
  return Buffer.substr(SelMin(), SelMax() - SelMin());
}

void UGuiTextInput::SelectAll()
{
  SelAnchor = 0;
  SelEnd = Buffer.size();
  CaretPos = Buffer.size();
}

void UGuiTextInput::CopySelectionToClipboard()
{
  if (!Clipboard)
  {
    return;
  }
  std::string text = GetSelectedText();
  if (text.empty())
  {
    text = Buffer;
  }
  Clipboard->SetString(text);
}

void UGuiTextInput::CutSelectionToClipboard()
{
  CopySelectionToClipboard();
  if (HasSelection())
  {
    DeleteSelection();
    NotifyEdited();
  }
}

void UGuiTextInput::PasteFromClipboard()
{
  if (!Clipboard)
  {
    return;
  }
  InsertText(
      SanitizeConsolePaste(Clipboard->GetString(), Buffer.size(), kMaxLength));
}

bool UGuiTextInput::HandleEditShortcut(const GuiKeyEvent &event)
{
  if (!Focused || !Enabled)
  {
    return false;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }
  if ((event.Mods & GuiKey::ModControl) == 0)
  {
    return false;
  }
  if (event.KeyCode == GuiKey::A)
  {
    SelectAll();
    return true;
  }
  if (event.KeyCode == GuiKey::C)
  {
    CopySelectionToClipboard();
    return true;
  }
  if (event.KeyCode == GuiKey::X)
  {
    CutSelectionToClipboard();
    return true;
  }
  if (event.KeyCode == GuiKey::V)
  {
    PasteFromClipboard();
    return true;
  }
  return false;
}

void UGuiTextInput::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  EnsureCaretVisible(renderer);
  renderer.DrawFilledRect(Bounds, Theme->ButtonNormal);
  const GuiRect clip = TextClipRect();
  const int top = TextTopY(renderer);
  const int lineH = TextLineHeight(renderer);
  const int textX = TextLeft() - ScrollX;
  renderer.PushClipRect(clip);
  if (HasSelection())
  {
    const int x0 = textX + renderer.MeasureTextWidth(Buffer.substr(0, SelMin()));
    const int x1 = textX + renderer.MeasureTextWidth(Buffer.substr(0, SelMax()));
    renderer.DrawFilledRect({x0, top, std::max(1, x1 - x0), lineH},
                            Theme->SlotSelectedFill);
  }
  renderer.DrawText(Buffer, textX, top, Theme->TextPrimary);
  if (Focused && !HasSelection())
  {
    const int cx = textX + renderer.MeasureTextWidth(Buffer.substr(0, CaretPos));
    const glm::vec4 caretColor(Theme->TextPrimary, 1.0f);
    renderer.DrawFilledRect({cx, top, 2, lineH}, caretColor);
  }
  renderer.PopClipRect();
  renderer.DrawBorderRect(Bounds,
                          Focused ? Theme->SlotSelected : Theme->PanelBorder,
                          Theme->BorderThickness);
  if (HasFocusHighlight())
  {
    DrawWidgetFocusRing(renderer, *Theme, Bounds);
  }
}

bool UGuiTextInput::PointerDown(const GuiMouseEvent &event,
                                UGuiRenderer &renderer)
{
  if (!Visible || event.Button != GuiMouseButton::Left ||
      !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  Focused = true;
  DraggingSelection = true;
#ifdef __ANDROID__
  AndroidSoftKeyboardSetTarget(this);
#endif
  CaretPos = CaretIndexFromX(event.X, renderer);
  SelAnchor = SelEnd = CaretPos;
  EnsureCaretVisible(renderer);
  return true;
}

bool UGuiTextInput::PointerMove(const GuiMouseEvent &event,
                                UGuiRenderer &renderer)
{
  if (!DraggingSelection || !Focused)
  {
    return false;
  }
  CaretPos = CaretIndexFromX(event.X, renderer);
  SelEnd = CaretPos;
  EnsureCaretVisible(renderer);
  return true;
}

bool UGuiTextInput::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Visible)
  {
    return false;
  }
  if (event.Button == GuiMouseButton::Right)
  {
    return Bounds.Contains(event.X, event.Y);
  }
  return Bounds.Contains(event.X, event.Y);
}

bool UGuiTextInput::OnMouseUp(const GuiMouseEvent &event)
{
  (void)event;
  DraggingSelection = false;
  return Focused;
}

bool UGuiTextInput::OnMouseMove(const GuiMouseEvent &event)
{
  (void)event;
  return DraggingSelection && Focused;
}

bool UGuiTextInput::OnChar(const GuiCharEvent &event)
{
  if (!Focused || !Enabled)
  {
    return false;
  }
  if (SuppressCharCodepoint != 0 && event.Codepoint == SuppressCharCodepoint)
  {
    SuppressCharCodepoint = 0;
    return true;
  }
  if (event.Codepoint >= 32 && event.Codepoint < 127)
  {
    if (Buffer.size() >= kMaxLength)
    {
      return true;
    }
    DeleteSelection();
    Buffer.insert(CaretPos, 1, static_cast<char>(event.Codepoint));
    ++CaretPos;
    NotifyEdited();
    return true;
  }
  return false;
}

bool UGuiTextInput::OnKey(const GuiKeyEvent &event)
{
  if (!Focused || !Enabled)
  {
    return false;
  }
  if (HandleEditShortcut(event))
  {
    return true;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }
  const bool shift = (event.Mods & GuiKey::ModShift) != 0;

  if (event.KeyCode == GuiKey::Delete)
  {
    if (HasSelection())
    {
      DeleteSelection();
    }
    else if (CaretPos < Buffer.size())
    {
      Buffer.erase(CaretPos, 1);
    }
    NotifyEdited();
    return true;
  }
  if (event.KeyCode == GuiKey::Backspace)
  {
    if (HasSelection())
    {
      DeleteSelection();
    }
    else if (CaretPos > 0)
    {
      Buffer.erase(CaretPos - 1, 1);
      --CaretPos;
    }
    NotifyEdited();
    return true;
  }
  if (event.KeyCode == GuiKey::Left)
  {
    if (CaretPos > 0)
    {
      --CaretPos;
    }
    if (shift)
    {
      SelEnd = CaretPos;
    }
    else
    {
      ClearSelection();
    }
    return true;
  }
  if (event.KeyCode == GuiKey::Right)
  {
    if (CaretPos < Buffer.size())
    {
      ++CaretPos;
    }
    if (shift)
    {
      SelEnd = CaretPos;
    }
    else
    {
      ClearSelection();
    }
    return true;
  }
  if (event.KeyCode == GuiKey::Home)
  {
    CaretPos = 0;
    if (shift)
    {
      SelEnd = 0;
    }
    else
    {
      ClearSelection();
    }
    return true;
  }
  if (event.KeyCode == GuiKey::End)
  {
    CaretPos = Buffer.size();
    if (shift)
    {
      SelEnd = CaretPos;
    }
    else
    {
      ClearSelection();
    }
    return true;
  }
  if (event.KeyCode >= GuiKey::A && event.KeyCode <= GuiKey::Z)
  {
    char c = static_cast<char>('a' + (event.KeyCode - GuiKey::A));
    if ((event.Mods & GuiKey::ModShift) != 0)
    {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    SuppressCharCodepoint = static_cast<unsigned int>(c);
    InsertText(std::string(1, c));
    return true;
  }
  if (event.KeyCode >= GuiKey::Digit0 && event.KeyCode <= GuiKey::Digit9)
  {
    const char c = static_cast<char>('0' + (event.KeyCode - GuiKey::Digit0));
    SuppressCharCodepoint = static_cast<unsigned int>(c);
    InsertText(std::string(1, c));
    return true;
  }
  if (event.KeyCode == GuiKey::Space)
  {
    SuppressCharCodepoint = static_cast<unsigned int>(' ');
    InsertText(" ");
    return true;
  }
  return false;
}

} // namespace cutum
