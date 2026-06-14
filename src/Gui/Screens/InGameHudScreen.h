#ifndef IN_GAME_HUD_SCREEN_H
#define IN_GAME_HUD_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Game/Inventory/SlotInteraction.h"
#include <memory>
#include <vector>

namespace cutum
{

class UGameSession;
class IGuiIconSource;
class UGuiSlot;
class UGuiPanel;
class UGuiLabel;
struct GuiTheme;

class UInGameHudScreen : public UGuiScreenBase
{
public:
  UInGameHudScreen(UGameSession *session, const GuiTheme *theme,
                   IGuiIconSource *icons);

  bool PickSlot(int x, int y, SlotAddress &out);

  void Update(double dt) override;
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  void SetPointerPosition(int x, int y);
  /// Обновить текстуры слотов; вызывать после отрисовки мира (FBO-иконки
  /// prefab).
  void SyncSlotIcons();

private:
  void EnsureHotbarWidgets();
  void LayoutHotbar();
  void UpdateSlotData();
  void UpdateTooltips();

  UGameSession *session_{nullptr};
  IGuiIconSource *icons_{nullptr};
  const GuiTheme *theme_;
  UGuiPanel *rootPanel_{nullptr};
  std::vector<UGuiSlot *> primarySlots_;
  std::vector<UGuiSlot *> secondarySlots_;
  UGuiLabel *tooltip_{nullptr};
  int pointerX_{-1};
  int pointerY_{-1};
  bool hotbarBuilt_{false};
};

} // namespace cutum

#endif
