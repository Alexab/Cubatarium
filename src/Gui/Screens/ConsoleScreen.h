#ifndef CONSOLE_SCREEN_H
#define CONSOLE_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include "Gui/GuiTheme.h"
#include "Gui/GuiTypes.h"
#include <memory>
#include <string>

namespace cutum {

class UGameSession;
class UGuiContext;
class UGuiListView;
class UGuiPopupMenu;
class UGuiRenderer;
class UGuiTextInput;

class UConsoleScreen : public UGuiScreenBase {
public:
    explicit UConsoleScreen(UGameSession* session);

    void Build(UGuiContext& ctx) override;
    void Update(double dt) override;
    void OnViewportChanged(int width, int height) override;
    bool BlocksGameInput() const override { return visible_; }

    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }
    void Toggle();
    void SubmitCommand();
    void AttachPopup(UGuiPopupMenu* popup);

    bool RouteKey(const GuiKeyEvent& event);
    bool RouteChar(const GuiCharEvent& event);
    bool RouteMouseButton(const GuiMouseEvent& event, UGuiRenderer& renderer);
    bool RouteMouseMove(const GuiMouseEvent& event, UGuiRenderer& renderer);
    bool IsPopupOpen() const;

private:
    void Relayout();
    void OnInputEdited();
    bool HandleHistoryNavigation(const GuiKeyEvent& event);
    void OpenContextMenu(int x, int y);

    UGameSession* session_{nullptr};
    UGuiListView* logView_{nullptr};
    UGuiTextInput* input_{nullptr};
    UGuiPopupMenu* popup_{nullptr};
    GuiTheme consoleTheme_{};
    bool visible_{false};
    int historyBrowseFromEnd_{-1};
    std::string draftLine_;
    bool draftValid_{false};
};

} // namespace cutum

#endif
