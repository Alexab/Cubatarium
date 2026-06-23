#include "Gui/Widgets/GuiTouchControls.h"

#include "App/Platform/InputManager.h"
#include "App/Platform/TouchInputBridge.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiPanel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

namespace
{

constexpr int kButtonSizeBase = 64;
constexpr int kMarginBase = 16;
constexpr int kJoystickSizeBase = 200;
constexpr int kTopRightStackOffsetBase = 132;
constexpr int kTouchControlsZOrder = 100;

class UTouchControlPanel : public UGuiPanel
{
public:
  explicit UTouchControlPanel(const GuiTheme *theme) : UGuiPanel(theme) {}

  UGuiWidget *HitTest(int x, int y) override
  {
    if (!Visible)
    {
      return nullptr;
    }
    std::vector<UGuiWidget *> sorted;
    sorted.reserve(Children.size());
    for (auto &child : Children)
    {
      sorted.push_back(child.get());
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const UGuiWidget *a, const UGuiWidget *b)
              { return a->GetZOrder() > b->GetZOrder(); });
    for (UGuiWidget *child : sorted)
    {
      if (UGuiWidget *hit = child->HitTest(x, y))
      {
        return hit;
      }
    }
    return nullptr;
  }
};

class UTouchHoldButton : public UGuiButton
{
public:
  UTouchHoldButton(const GuiTheme *theme, std::string label, KeyCode key,
                   UTouchInputBridge *bridge,
                   std::function<void()> onPress = nullptr)
      : UGuiButton(theme, std::move(label)), key_(key), Bridge(bridge),
        onPress_(std::move(onPress))
  {
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!UGuiButton::OnMouseDown(event))
    {
      return false;
    }
    capturePointerId_ = event.PointerId;
    if (onPress_)
    {
      onPress_();
    }
    held_ = true;
    if (Bridge)
    {
      Bridge->SetHeldKey(key_, true);
    }
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!held_ || !GuiPointerMatches(event.PointerId, capturePointerId_))
    {
      return false;
    }
    const bool handled = UGuiButton::OnMouseUp(event);
    if (Bridge)
    {
      Bridge->SetHeldKey(key_, false);
    }
    held_ = false;
    capturePointerId_ = -1;
    return handled;
  }

  void ForceRelease()
  {
    held_ = false;
    capturePointerId_ = -1;
    if (Bridge)
    {
      Bridge->SetHeldKey(key_, false);
    }
  }

private:
  KeyCode key_;
  UTouchInputBridge *Bridge;
  std::function<void()> onPress_;
  bool held_{false};
  int capturePointerId_{-1};
};

class UTouchToggleButton : public UGuiButton
{
public:
  UTouchToggleButton(const GuiTheme *theme, std::string label,
                     UTouchInputBridge *bridge)
      : UGuiButton(theme, std::move(label)), Bridge(bridge)
  {
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!UGuiButton::OnMouseDown(event))
    {
      return false;
    }
    toggled_ = !toggled_;
    if (Bridge)
    {
      Bridge->SetSprintActive(toggled_);
    }
    return true;
  }

  void ForceRelease()
  {
    toggled_ = false;
    if (Bridge)
    {
      Bridge->SetSprintActive(false);
    }
  }

private:
  UTouchInputBridge *Bridge;
  bool toggled_{false};
};

class UTouchVirtualJoystick : public UGuiWidget
{
public:
  UTouchVirtualJoystick(const GuiTheme *theme, UTouchInputBridge *bridge)
      : Theme(theme), Bridge(bridge)
  {
  }

  bool IsActive() const { return active_; }

  void ForceRelease()
  {
    active_ = false;
    capturePointerId_ = -1;
    knobOffset_ = {0.f, 0.f};
    if (Bridge)
    {
      Bridge->SetJoystickVector({0.f, 0.f});
      Bridge->SetJoystickActive(false);
    }
  }

  void ResyncPointer(int x, int y)
  {
    if (active_)
    {
      UpdateStick(x, y);
    }
  }

  bool OnCapturedMove(int PointerId, int x, int y)
  {
    if (!active_ || !GuiPointerMatches(PointerId, capturePointerId_))
    {
      return false;
    }
    UpdateStick(x, y);
    return true;
  }

  void Draw(UGuiRenderer &renderer) override
  {
    if (!Visible || !Theme)
    {
      return;
    }
    renderer.DrawFilledRect(Bounds, {0.12f, 0.12f, 0.14f, 0.45f});
    renderer.DrawBorderRect(Bounds, {1.f, 1.f, 1.f, 0.35f},
                            Theme->BorderThickness);
    renderer.DrawFilledRect(ComputeKnobRect(), {0.92f, 0.92f, 0.95f, 0.85f});
    renderer.DrawBorderRect(ComputeKnobRect(), {1.f, 1.f, 1.f, 0.5f}, 1);
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y) ||
        event.Button != GuiMouseButton::Left)
    {
      return false;
    }
    active_ = true;
    capturePointerId_ = event.PointerId;
    if (Bridge)
    {
      Bridge->SetJoystickActive(true);
      Bridge->RequestCameraBaseline(static_cast<float>(event.X),
                                    static_cast<float>(event.Y));
    }
    UpdateStick(event.X, event.Y);
    return true;
  }

  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.PointerId, capturePointerId_))
    {
      return false;
    }
    UpdateStick(event.X, event.Y);
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.PointerId, capturePointerId_))
    {
      return false;
    }
    ForceRelease();
    capturePointerId_ = -1;
    return event.Button == GuiMouseButton::Left;
  }

private:
  void UpdateStick(int x, int y)
  {
    const float centerX = static_cast<float>(Bounds.X + Bounds.W / 2);
    const float centerY = static_cast<float>(Bounds.Y + Bounds.H / 2);
    float dx = static_cast<float>(x) - centerX;
    float dy = centerY - static_cast<float>(y);
    const float maxRadius = static_cast<float>(Bounds.W) * 0.38f;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > maxRadius && length > 0.f)
    {
      dx = dx / length * maxRadius;
      dy = dy / length * maxRadius;
    }
    knobOffset_ = {dx, -dy};
    if (Bridge)
    {
      const glm::vec2 normalized =
          maxRadius > 0.f ? glm::vec2(dx / maxRadius, dy / maxRadius)
                          : glm::vec2{0.f, 0.f};
      Bridge->SetJoystickVector(normalized);
    }
  }

  GuiRect ComputeKnobRect() const
  {
    const int knobSize = std::max(28, Bounds.W / 3);
    const float centerX =
        static_cast<float>(Bounds.X + Bounds.W / 2) + knobOffset_.x;
    const float centerY =
        static_cast<float>(Bounds.Y + Bounds.H / 2) + knobOffset_.y;
    return {static_cast<int>(centerX - knobSize / 2),
            static_cast<int>(centerY - knobSize / 2), knobSize, knobSize};
  }

  const GuiTheme *Theme{nullptr};
  UTouchInputBridge *Bridge{nullptr};
  glm::vec2 knobOffset_{0.f, 0.f};
  bool active_{false};
  int capturePointerId_{-1};
};

class UTouchLookPad : public UGuiWidget
{
public:
  UTouchLookPad(const GuiTheme *theme, const float *uiScale,
                UTouchInputBridge *bridge)
      : Theme(theme), UiScale(uiScale), Bridge(bridge)
  {
  }

  UGuiWidget *HitTest(int x, int y) override
  {
    if (!Visible || !Enabled || !Bounds.Contains(x, y))
    {
      return nullptr;
    }
    return this;
  }

  bool OnCapturedMove(int PointerId, int x, int y)
  {
    if (!active_ || !GuiPointerMatches(PointerId, capturePointerId_))
    {
      return false;
    }
    return OnLookMove(x, y);
  }

  void Draw(UGuiRenderer &renderer) override
  {
    if (!Visible || !Theme)
    {
      return;
    }
    const float scale = UiScale ? *UiScale : 1.f;
    renderer.DrawFilledRect(Bounds, {0.10f, 0.12f, 0.16f, 0.35f});
    renderer.DrawBorderRect(Bounds, {1.f, 1.f, 1.f, 0.28f},
                            Theme->BorderThickness);
    GuiRect labelRect = Bounds;
    labelRect.Y += Bounds.H / 2 - ScalePx(12, scale);
    labelRect.H = ScalePx(24, scale);
    renderer.DrawTextCenteredInRect(labelRect, "Look", {0.92f, 0.92f, 0.95f});
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y) ||
        event.Button != GuiMouseButton::Left)
    {
      return false;
    }
    active_ = true;
    capturePointerId_ = event.PointerId;
    Dragged = false;
    startX_ = event.X;
    startY_ = event.Y;
    lastX_ = event.X;
    lastY_ = event.Y;
    downTime_ = std::chrono::steady_clock::now();
    if (Bridge)
    {
      Bridge->RequestCameraBaseline(static_cast<float>(event.X),
                                    static_cast<float>(event.Y));
    }
    return true;
  }

  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.PointerId, capturePointerId_))
    {
      return false;
    }
    return OnLookMove(event.X, event.Y);
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.PointerId, capturePointerId_))
    {
      return false;
    }
    if (Bridge && !Dragged)
    {
      const float holdSeconds =
          std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                       downTime_)
              .count();
      const float dragDistance = std::sqrt(
          static_cast<float>((event.X - startX_) * (event.X - startX_) +
                             (event.Y - startY_) * (event.Y - startY_)));
      const float slop = UiScale ? std::min(36.f * *UiScale, 56.f) : 36.f;
      if (dragDistance < slop && holdSeconds < 0.50f)
      {
        Bridge->QueuePlaceTap(static_cast<float>(event.X),
                              static_cast<float>(event.Y));
      }
    }
    active_ = false;
    Dragged = false;
    capturePointerId_ = -1;
    return true;
  }

private:
  bool OnLookMove(int x, int y)
  {
    if (!active_ || !Bridge)
    {
      return false;
    }
    const float dx = static_cast<float>(x - lastX_);
    const float dy = static_cast<float>(y - lastY_);
    const float dragDistance = std::sqrt(static_cast<float>(
        (x - startX_) * (x - startX_) + (y - startY_) * (y - startY_)));
    const float threshold = UiScale ? 10.f * *UiScale : 10.f;
    if (!Dragged && dragDistance > threshold)
    {
      Dragged = true;
      Bridge->RequestCameraBaseline(static_cast<float>(x),
                                    static_cast<float>(y));
    }
    if (Dragged)
    {
      Bridge->AddLookDelta(dx, dy);
    }
    lastX_ = x;
    lastY_ = y;
    return true;
  }

  const GuiTheme *Theme{nullptr};
  const float *UiScale{nullptr};
  UTouchInputBridge *Bridge{nullptr};
  int startX_{0};
  int startY_{0};
  int lastX_{0};
  int lastY_{0};
  bool active_{false};
  bool Dragged{false};
  int capturePointerId_{-1};
  std::chrono::steady_clock::time_point downTime_{};
};

} // namespace

UGuiTouchControls::UGuiTouchControls(const GuiTheme *theme,
                                     UTouchInputBridge *bridge,
                                     std::function<void()> onMenu,
                                     std::function<void()> onInventory,
                                     std::function<void()> onConsole,
                                     std::function<void()> onJumpPress)
    : Theme(theme), Bridge(bridge), OnMenu(std::move(onMenu)),
      OnInventory(std::move(onInventory)), OnConsole(std::move(onConsole)),
      OnJumpPress(std::move(onJumpPress))
{
}

UGuiTouchControls::~UGuiTouchControls() = default;

void UGuiTouchControls::Build(UGuiPanel *parent)
{
  if (!parent || !Theme || !Bridge)
  {
    return;
  }
  auto panel = std::make_unique<UTouchControlPanel>(Theme);
  panel->SetDrawBackground(false);
  panel->SetZOrder(kTouchControlsZOrder);
  Root = panel.get();
  parent->AddChild(std::move(panel));

  auto joystick = std::make_unique<UTouchVirtualJoystick>(Theme, Bridge);
  auto *joystickWidget = joystick.get();
  JoystickWidget = joystickWidget;
  Root->AddChild(std::move(joystick));

  auto Jump = std::make_unique<UTouchHoldButton>(Theme, "Jump",
                                                 KeyCode::Key_Space, Bridge,
                                                 [this]()
                                                 {
                                                   if (OnJumpPress)
                                                   {
                                                     OnJumpPress();
                                                   }
                                                 });
  auto *jumpWidget = Jump.get();
  Jump->SetZOrder(kTouchControlsZOrder + 2);
  JumpButton = jumpWidget;
  Root->AddChild(std::move(Jump));

  auto Sneak = std::make_unique<UTouchHoldButton>(Theme, "Sneak",
                                                  KeyCode::Key_Shift, Bridge);
  auto *sneakWidget = Sneak.get();
  Sneak->SetZOrder(kTouchControlsZOrder + 2);
  SneakButton = sneakWidget;
  Root->AddChild(std::move(Sneak));

  auto Sprint = std::make_unique<UTouchToggleButton>(Theme, "Sprint", Bridge);
  auto *sprintWidget = Sprint.get();
  Sprint->SetZOrder(kTouchControlsZOrder + 2);
  SprintButton = sprintWidget;
  Root->AddChild(std::move(Sprint));

  auto inventory = std::make_unique<UGuiButton>(Theme, "Inv");
  inventory->SetOnClick(
      [this]()
      {
        if (OnInventory)
        {
          OnInventory();
        }
      });
  InventoryButton = inventory.get();
  Root->AddChild(std::move(inventory));

  auto menu = std::make_unique<UGuiButton>(Theme, "Menu");
  menu->SetOnClick(
      [this]()
      {
        if (OnMenu)
        {
          OnMenu();
        }
      });
  MenuButton = menu.get();
  Root->AddChild(std::move(menu));

  auto console = std::make_unique<UGuiButton>(Theme, "Cmd");
  console->SetOnClick(
      [this]()
      {
        if (OnConsole)
        {
          OnConsole();
        }
      });
  ConsoleButton = console.get();
  Root->AddChild(std::move(console));

  auto lookPad = std::make_unique<UTouchLookPad>(Theme, &UiScale, Bridge);
  lookPad->SetZOrder(kTouchControlsZOrder + 1);
  LookPad = lookPad.get();
  Root->AddChild(std::move(lookPad));

  OnRouteCapturedMove =
      [joystickWidget, lookPad = LookPad](int PointerId, int x, int y)
  {
    if (static_cast<UTouchVirtualJoystick *>(joystickWidget)
            ->OnCapturedMove(PointerId, x, y))
    {
      return true;
    }
    if (lookPad)
    {
      return static_cast<UTouchLookPad *>(lookPad)->OnCapturedMove(PointerId, x,
                                                                   y);
    }
    return false;
  };
  OnReleaseJoystickCapture = [joystickWidget]()
  { joystickWidget->ForceRelease(); };
  OnReleaseHoldButtons = [jumpWidget, sneakWidget, sprintWidget]()
  {
    jumpWidget->ForceRelease();
    sneakWidget->ForceRelease();
    sprintWidget->ForceRelease();
  };
  OnReleaseAllCaptures = [joystickWidget, jumpWidget, sneakWidget,
                          sprintWidget]()
  {
    joystickWidget->ForceRelease();
    jumpWidget->ForceRelease();
    sneakWidget->ForceRelease();
    sprintWidget->ForceRelease();
  };
}

bool UGuiTouchControls::RouteCapturedMove(int PointerId, int x, int y)
{
  return OnRouteCapturedMove ? OnRouteCapturedMove(PointerId, x, y) : false;
}

void UGuiTouchControls::ReleaseJoystickCapture()
{
  if (OnReleaseJoystickCapture)
  {
    OnReleaseJoystickCapture();
  }
  if (Bridge)
  {
    Bridge->ResetJoystick();
  }
}

void UGuiTouchControls::ReleaseAllCaptures()
{
  if (OnReleaseAllCaptures)
  {
    OnReleaseAllCaptures();
  }
  if (Bridge)
  {
    Bridge->ResetJoystick();
  }
}

void UGuiTouchControls::Layout(int width, int height, int offsetX, int offsetY,
                               float uiScale)
{
  if (!Root)
  {
    return;
  }
  if (width == lastLayoutWidth_ && height == lastLayoutHeight_ &&
      offsetX == lastLayoutOffsetX_ && offsetY == lastLayoutOffsetY_ &&
      std::fabs(uiScale - lastLayoutUiScale_) < 0.01f)
  {
    return;
  }
  const bool hadLayout = lastLayoutWidth_ >= 0;
  lastLayoutWidth_ = width;
  lastLayoutHeight_ = height;
  lastLayoutOffsetX_ = offsetX;
  lastLayoutOffsetY_ = offsetY;
  lastLayoutUiScale_ = uiScale;
  if (hadLayout && OnReleaseHoldButtons)
  {
    OnReleaseHoldButtons();
  }

  UiScale = uiScale;
  Root->SetBounds({0, 0, width, height});

  const int buttonSize = ScalePx(kButtonSizeBase, uiScale);
  const int margin =
      std::max(ScalePx(24, uiScale), ScalePx(kMarginBase, uiScale));
  const int shortEdge = std::min(width, height);
  const int joystickTarget = ScalePx(kJoystickSizeBase, uiScale);
  const int joystickMax =
      std::max(ScalePx(120, uiScale), static_cast<int>(shortEdge * 0.20f));
  const int joystickSize = std::min(joystickTarget, joystickMax);
  const int buttonGap = ScalePx(8, uiScale);
  const int controlLift = ScalePx(8, uiScale);

  const int bottomRowY = height - buttonSize - margin;
  const int joystickY = bottomRowY - joystickSize - controlLift;
  const int leftMargin = std::max(margin, offsetX + ScalePx(12, uiScale));
  const int leftControlsW = leftMargin + joystickSize + margin;
  const int leftControlsTop = std::max(0, joystickY - controlLift);
  const int leftControlsH = std::max(1, height - leftControlsTop);
  const int rightColX = width - margin - buttonSize;
  const int jumpX = rightColX - buttonSize - buttonGap;
  const int topRightY = margin + ScalePx(kTopRightStackOffsetBase, uiScale);
  const int topRightStackH = buttonSize * 3 + buttonGap * 2 + margin;
  if (JoystickWidget)
  {
    JoystickWidget->SetBounds(
        {leftMargin, joystickY, joystickSize, joystickSize});
  }
  if (JumpButton)
  {
    JumpButton->SetBounds({jumpX, bottomRowY, buttonSize, buttonSize});
  }
  if (SneakButton)
  {
    SneakButton->SetBounds({rightColX, bottomRowY, buttonSize, buttonSize});
  }
  if (SprintButton)
  {
    SprintButton->SetBounds({rightColX, bottomRowY - buttonSize - buttonGap,
                             buttonSize, buttonSize});
  }
  if (MenuButton)
  {
    MenuButton->SetBounds(
        {width - buttonSize - margin, topRightY, buttonSize, buttonSize});
  }
  if (InventoryButton)
  {
    InventoryButton->SetBounds({width - buttonSize - margin,
                                topRightY + buttonSize + buttonGap, buttonSize,
                                buttonSize});
  }
  if (ConsoleButton)
  {
    ConsoleButton->SetBounds({width - buttonSize - margin,
                              topRightY + (buttonSize + buttonGap) * 2,
                              buttonSize, buttonSize});
  }

  if (LookPad)
  {
    const int lookSize = std::max(joystickSize, buttonSize * 2 + buttonGap);
    const int lookX = width - margin - lookSize;
    const int lookY = bottomRowY - lookSize - controlLift;
    LookPad->SetBounds({lookX, lookY, lookSize, lookSize});
  }

  if (Bridge)
  {
    Bridge->ClearBlockedGameRegions();
    Bridge->SetBlockedGameRegion(0, {offsetX + leftMargin,
                                     offsetY + leftControlsTop, leftControlsW,
                                     leftControlsH});
    Bridge->SetBlockedGameRegion(
        1, {offsetX + std::max(0, width - buttonSize - margin * 2),
            offsetY + topRightY - margin / 2, buttonSize + margin * 2,
            topRightStackH});
    const int actionPad = ScalePx(4, uiScale);
    const int actionRowH = buttonSize * 2 + buttonGap;
    Bridge->SetBlockedGameRegion(2,
                                 {offsetX + std::max(0, jumpX - actionPad),
                                  offsetY + std::max(0, bottomRowY - actionRowH -
                                                                  actionPad),
                                  buttonSize * 2 + buttonGap + actionPad * 2,
                                  actionRowH + actionPad * 2});
  }

  if (JoystickWidget && Bridge)
  {
    if (auto *joystick = dynamic_cast<UTouchVirtualJoystick *>(JoystickWidget))
    {
      if (joystick->IsActive())
      {
        const glm::vec2 pos = Bridge->GetMousePosition();
        joystick->ResyncPointer(static_cast<int>(pos.x),
                                static_cast<int>(pos.y));
      }
    }
  }
}

} // namespace cutum
