#ifndef I_GUI_GAME_ACTIONS_H
#define I_GUI_GAME_ACTIONS_H

namespace cutum {

class IGuiGameActions {
public:
    virtual ~IGuiGameActions() = default;
    virtual void StartGame() = 0;
    virtual void QuitApplication() = 0;
    virtual void OpenSettings() = 0;
};

} // namespace cutum

#endif
