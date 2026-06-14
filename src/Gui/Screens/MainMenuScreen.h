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

  bool IsQuitConfirmationVisible() const { return quitDialogVisible_; }
  void ShowQuitConfirmation(bool visible);

private:
  void Relayout();
  void RelayoutQuitDialog();

  IGuiGameActions *actions_{nullptr};
  UGuiLabel *title_{nullptr};
  UGuiLabel *versionLabel_{nullptr};
  std::vector<UGuiButton *> buttons_;

  UGuiPanel *quitBackdrop_{nullptr};
  UGuiPanel *quitDialog_{nullptr};
  UGuiLabel *quitMessage_{nullptr};
  UGuiButton *quitYesButton_{nullptr};
  UGuiButton *quitNoButton_{nullptr};
  bool quitDialogVisible_{false};
};

} // namespace cutum

#endif
