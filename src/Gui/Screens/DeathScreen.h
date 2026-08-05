#ifndef DEATH_SCREEN_H
#define DEATH_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <functional>
#include <string>

namespace cutum
{

class UGuiPanel;
class UGuiLabel;
class UGuiButton;
struct GuiTheme;

class UDeathScreen : public UGuiScreenBase
{
public:
  void Build(UGuiContext &ctx) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void SetCause(const std::string &cause);
  void SetOnRespawn(std::function<void()> handler);
  void SetOnSpectate(std::function<void()> handler);

private:
  void Relayout();

  const GuiTheme *Theme{nullptr};
  UGuiPanel *Panel{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *CauseLabel{nullptr};
  UGuiButton *RespawnBtn{nullptr};
  UGuiButton *SpectateBtn{nullptr};
  bool Visible{false};
  std::function<void()> OnRespawn;
  std::function<void()> OnSpectate;
};

} // namespace cutum

#endif
