#ifndef CONSOLE_SCREEN_H
#define CONSOLE_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include "Gui/GuiTheme.h"
#include "Gui/GuiTypes.h"
#include <memory>
#include <string>

namespace cutum {

class GameSession;
class GuiContext;
class GuiListView;
class GuiPopupMenu;
class GuiRenderer;
class GuiTextInput;

class ConsoleScreen : public GuiScreenBase {
public:
    explicit ConsoleScreen(GameSession* session);

    void Build(GuiContext& ctx) override;
    void Update(double dt) override;
    void OnViewportChanged(int width, int height) override;
    bool BlocksGameInput() const override { return visible_; }

    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }
    void Toggle();
    void SubmitCommand();
    void AttachPopup(GuiPopupMenu* popup);

    bool RouteKey(const GuiKeyEvent& event);
    bool RouteChar(const GuiCharEvent& event);
    bool RouteMouseButton(const GuiMouseEvent& event, GuiRenderer& renderer);
    bool RouteMouseMove(const GuiMouseEvent& event, GuiRenderer& renderer);
    bool IsPopupOpen() const;

private:
    void Relayout();
    void OnInputEdited();
    bool HandleHistoryNavigation(const GuiKeyEvent& event);
    void OpenContextMenu(int x, int y);

    GameSession* session_{nullptr};
    GuiListView* logView_{nullptr};
    GuiTextInput* input_{nullptr};
    GuiPopupMenu* popup_{nullptr};
    GuiTheme consoleTheme_{};
    bool visible_{false};
    int historyBrowseFromEnd_{-1};
    std::string draftLine_;
    bool draftValid_{false};
};

} // namespace cutum

#endif
