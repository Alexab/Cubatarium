#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum
{

class IGuiGameActions;
class UGuiButton;
class UGuiLabel;
class UGuiPanel;
struct GuiTheme;

class UMainMenuScreen : public UGuiScreenBase
{
public:
  explicit UMainMenuScreen(IGuiGameActions *actions);

  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return true; }

  bool IsQuitConfirmationVisible() const { return QuitDialogVisible; }
  void ShowQuitConfirmation(bool visible);

private:
  void Relayout();
  void RelayoutQuitDialog();

  IGuiGameActions *Actions{nullptr};
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
