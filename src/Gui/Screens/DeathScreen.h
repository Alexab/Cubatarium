#ifndef DEATH_SCREEN_H
#define DEATH_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include <array>
#include <string>

namespace cutum
{

class UGuiPanel;
class UGuiLabel;
class UGuiButton;
class UApplication;

struct DeathScreenStorage
{
  size_t screen_bytes{}, widgets{}, widget_bytes{}, children_bytes{}, text_bytes{};
  size_t callback_bytes{}, requested{};
  std::array<bool, 3> callback_present{}, callback_supported{};
  bool present{}, visible{}, hardcore{}, bindings_match{}, supported{};
};

class UDeathScreen : public UGuiScreenBase
{
public:
  explicit UDeathScreen(UApplication &application);
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void SetCause(const std::string &cause);
  void SetHardcore(bool hardcore);
  DeathScreenStorage InspectOwnedStorage(const UGuiContext &context,
                                         const UApplication &application) const noexcept;

private:
  template<void (UApplication::*Action)()> struct ButtonAction;
  void Relayout();

  UApplication &Application;
  UGuiPanel *Panel{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *CauseLabel{nullptr};
  UGuiButton *RespawnBtn{nullptr};
  UGuiButton *SpectateBtn{nullptr};
  UGuiButton *ReturnToMenuBtn{nullptr};
  bool Visible{false};
  bool Hardcore{false};
};

} // namespace cutum

#endif
