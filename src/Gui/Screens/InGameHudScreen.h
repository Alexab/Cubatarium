#ifndef IN_GAME_HUD_SCREEN_H
#define IN_GAME_HUD_SCREEN_H

#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiScreenBase.h"
#include <memory>
#include <vector>
#if defined(__ANDROID__)
#include "Gui/Widgets/GuiTouchControls.h"
#include <functional>
#endif

namespace cutum
{

class UGameSession;
class IUGuiIconSource;
class UGuiSlot;
class UGuiPanel;
class UGuiLabel;
class UGuiRenderer;
struct GuiTheme;
class UGuiContext;

class UInGameHudScreen : public UGuiScreenBase
{
public:
  UInGameHudScreen(UGameSession *session, const GuiTheme *theme,
                   IUGuiIconSource *icons);
  ~UInGameHudScreen();

  bool PickSlot(int x, int y, SlotAddress &out);

  void Update(double dt) override;
  void Build(UGuiContext &ctx) override;
  void OnViewportChanged(int width, int height) override;
  void SetPointerPosition(int x, int y);
#if defined(__ANDROID__)
  void ConfigureTouchControls(class UTouchInputBridge *bridge,
                              std::function<void()> onMenu,
                              std::function<void()> onInventory,
                              std::function<void()> onConsole,
                              std::function<void()> onJumpPress,
                              TouchIsoControlCallbacks isoCallbacks = {});
  void InvalidateTouchControlsLayout();
  bool RouteTouchMove(int PointerId, int x, int y);
  void ReleaseJoystickCapture();
  void ReleaseJoystickCaptureForPointer(int pointer_id);
  void ReleaseTouchCaptures();
  bool HitTestTouchControls(int x, int y) const;
  void RenderTouchControlsOverlay(class UGuiContext &ctx, int width, int height);
#endif
  /// Обновить текстуры слотов; вызывать после отрисовки мира (FBO-иконки
  /// prefab).
  void SyncSlotIcons();

private:
  void EnsureHotbarWidgets();
  void LayoutHotbar();
  void UpdateSlotData();
  void UpdateTooltips();

  UGameSession *Session{nullptr};
  IUGuiIconSource *Icons{nullptr};
  const GuiTheme *Theme;
  UGuiPanel *RootPanel{nullptr};
  std::vector<UGuiSlot *> PrimarySlots;
  std::vector<UGuiSlot *> SecondarySlots;
  UGuiLabel *Tooltip{nullptr};
  UGuiRenderer *Renderer{nullptr};
  int PointerX{-1};
  int PointerY{-1};
  bool HotbarBuilt{false};
#if defined(__ANDROID__)
  std::unique_ptr<class UGuiTouchControls> TouchControls;
#endif
};

} // namespace cutum

#endif
