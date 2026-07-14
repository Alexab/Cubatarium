#ifndef GUI_TEXT_INPUT_H
#define GUI_TEXT_INPUT_H

#include "Gui/Interfaces/IUGuiClipboard.h"
#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <string>

namespace cutum
{

struct GuiTheme;

class UGuiTextInput : public UGuiWidget
{
public:
  static constexpr size_t kMaxLength = 256;

  explicit UGuiTextInput(const GuiTheme *theme);

  const std::string &GetText() const { return Buffer; }
  void SetText(const std::string &text);
  void SetFocused(bool focused) { Focused = focused; }
  bool IsFocused() const { return Focused; }

  void SetClipboard(IUGuiClipboard *clipboard) { Clipboard = clipboard; }
  void SetOnEdited(std::function<void()> fn) { OnEdited = std::move(fn); }

  void ClearSelection();
  bool HasSelection() const;
  std::string GetSelectedText() const;
  void SelectAll();
  void CopySelectionToClipboard();
  void CutSelectionToClipboard();
  void PasteFromClipboard();
  bool HandleEditShortcut(const GuiKeyEvent &event);

  bool CanFocus() const override;

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;
  bool PointerDown(const GuiMouseEvent &event, UGuiRenderer &renderer);
  bool PointerMove(const GuiMouseEvent &event, UGuiRenderer &renderer);
  bool OnKey(const GuiKeyEvent &event) override;
  bool OnChar(const GuiCharEvent &event) override;

  int GetPreferredHeight() const override;

private:
  size_t SelMin() const;
  size_t SelMax() const;
  void DeleteSelection();
  void InsertText(const std::string &text);
  void NotifyEdited();
  size_t CaretIndexFromX(int mouseX, UGuiRenderer &renderer) const;
  int TextLeft() const;
  int TextPadding() const;

  const GuiTheme *Theme;
  IUGuiClipboard *Clipboard{nullptr};
  std::function<void()> OnEdited;
  std::string Buffer;
  size_t CaretPos{0};
  size_t SelAnchor{0};
  size_t SelEnd{0};
  bool Focused{false};
  bool DraggingSelection{false};
  bool ProgrammaticChange{false};
  unsigned int SuppressCharCodepoint{0};
};

} // namespace cutum

#endif
