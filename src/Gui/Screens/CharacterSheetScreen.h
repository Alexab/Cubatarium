#ifndef CHARACTER_SHEET_SCREEN_H
#define CHARACTER_SHEET_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

class IUCharacterStatsViewModel;
class UGuiWindow;
class UGuiPanel;
class UGuiLabel;
struct GuiTheme;

class UCharacterSheetScreen : public UGuiScreenBase
{
public:
  explicit UCharacterSheetScreen(IUCharacterStatsViewModel *stats);
  ~UCharacterSheetScreen() override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();

private:
  void Relayout();
  void RefreshLabels();
  static std::string FormatBar(const char *name, float cur, float max);

  IUCharacterStatsViewModel *Stats{nullptr};
  UGuiWindow *Window{nullptr};
  UGuiLabel *TitleMeta{nullptr};
  UGuiLabel *ModeLabel{nullptr};
  std::vector<UGuiLabel *> VitalLabels;
  std::vector<UGuiLabel *> AttrLabels;
  bool Visible{false};
  bool Built{false};
};

} // namespace cutum

#endif
