#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum
{

class IUGuiGameActions;
class UGuiButton;
class UGuiLabel;
class UGuiPanel;
struct GuiTheme;

struct MainMenuStorage
{
  size_t screen_bytes{}, widgets{}, widget_bytes{}, children_bytes{}, button_index_bytes{}, text_bytes{};
  size_t callbacks{}, callback_bytes{}, requested{};
  bool present{}, resume{}, quit_dialog{}, bindings_match{}, supported{};
};

class UMainMenuScreen : public UGuiScreenBase
{
public:
  explicit UMainMenuScreen(IUGuiGameActions *actions);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }

  bool IsQuitConfirmationVisible() const { return QuitDialogVisible; }
  void ShowQuitConfirmation(bool visible);
  MainMenuStorage InspectOwnedStorage(const UGuiContext &context,
                                       const IUGuiGameActions *actions) const noexcept;

private:
  struct PrimaryAction;
  template<void (IUGuiGameActions::*Invoke)()> struct MenuAction;
  template<bool Visible> struct QuitDialogAction;
  void Relayout();
  void RelayoutQuitDialog();

  IUGuiGameActions *Actions{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *VersionLabel{nullptr};
  std::vector<UGuiButton *> Buttons;

  UGuiPanel *QuitBackdrop{nullptr};
  UGuiPanel *QuitDialog{nullptr};
  UGuiLabel *QuitMessage{nullptr};
  UGuiButton *QuitYesButton{nullptr};
  UGuiButton *QuitNoButton{nullptr};
  bool QuitDialogVisible{false};
};

} // namespace cutum

#endif
