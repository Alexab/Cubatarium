#ifndef IU_GUI_GAME_ACTIONS_H
#define IU_GUI_GAME_ACTIONS_H

namespace cutum
{

class IUGuiGameActions
{
public:
  virtual ~IUGuiGameActions() = default;
  virtual void LoadLastWorld() = 0;
  virtual void ResumeGame() = 0;
  virtual void OpenLoadWorld() = 0;
  virtual void OpenNewWorld() = 0;
  virtual void QuitApplication() = 0;
  virtual void OpenSettings() = 0;
  virtual void OpenWorldSettings() = 0;
  virtual int GetHotbarCountSetting() const { return 1; }
  virtual void SetHotbarCountSetting(int /*count*/) {}
  /// true после выхода в меню по Esc — мир уже загружен.
  virtual bool HasPausedSession() const { return false; }
};

} // namespace cutum

#endif
