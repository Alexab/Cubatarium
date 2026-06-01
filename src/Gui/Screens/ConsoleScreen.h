#ifndef CONSOLE_SCREEN_H
#define CONSOLE_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>

namespace cutum {

class GameSession;
class GuiListView;
class GuiTextInput;

class ConsoleScreen : public GuiScreenBase {
public:
    explicit ConsoleScreen(GameSession* session);

    void Build(GuiContext& ctx) override;
    void Update(double dt) override;
    bool BlocksGameInput() const override { return visible_; }

    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }
    void Toggle();
    void SubmitCommand();

private:

    GameSession* session_{nullptr};
    GuiListView* logView_{nullptr};
    GuiTextInput* input_{nullptr};
    bool visible_{false};
};

} // namespace cutum

#endif
