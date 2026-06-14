#ifndef GUI_TEXT_INPUT_H
#define GUI_TEXT_INPUT_H

#include "Gui/Interfaces/IGuiClipboard.h"
#include "GuiWidget.h"
#include <functional>
#include <string>

namespace cutum {

struct GuiTheme;

class UGuiTextInput : public UGuiWidget {
public:
    static constexpr size_t kMaxLength = 256;

    explicit UGuiTextInput(const GuiTheme* theme);

    const std::string& GetText() const { return buffer_; }
    void SetText(const std::string& text);
    void SetFocused(bool focused) { focused_ = focused; }
    bool IsFocused() const { return focused_; }

    void SetClipboard(IGuiClipboard* clipboard) { Clipboard = clipboard; }
    void SetOnEdited(std::function<void()> fn) { onEdited_ = std::move(fn); }

    void ClearSelection();
    bool HasSelection() const;
    std::string GetSelectedText() const;
    void SelectAll();
    void CopySelectionToClipboard();
    void CutSelectionToClipboard();
    void PasteFromClipboard();
    bool HandleEditShortcut(const GuiKeyEvent& event);

    bool CanFocus() const override;

    void Draw(UGuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnMouseUp(const GuiMouseEvent& event) override;
    bool OnMouseMove(const GuiMouseEvent& event) override;
    bool PointerDown(const GuiMouseEvent& event, UGuiRenderer& renderer);
    bool PointerMove(const GuiMouseEvent& event, UGuiRenderer& renderer);
    bool OnKey(const GuiKeyEvent& event) override;
    bool OnChar(const GuiCharEvent& event) override;

    int GetPreferredHeight() const override;

private:
    size_t SelMin() const;
    size_t SelMax() const;
    void DeleteSelection();
    void InsertText(const std::string& text);
    void NotifyEdited();
    size_t CaretIndexFromX(int mouseX, UGuiRenderer& renderer) const;
    int TextLeft() const;
    int TextPadding() const;

    const GuiTheme* theme_;
    IGuiClipboard* Clipboard{nullptr};
    std::function<void()> onEdited_;
    std::string buffer_;
    size_t caretPos_{0};
    size_t selAnchor_{0};
    size_t selEnd_{0};
    bool focused_{false};
    bool draggingSelection_{false};
    bool programmaticChange_{false};
    unsigned int suppressCharCodepoint_{0};
};

} // namespace cutum

#endif
