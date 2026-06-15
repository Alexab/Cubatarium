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

class TouchControlPanel : public UGuiPanel
{
public:
  explicit TouchControlPanel(const GuiTheme *theme) : UGuiPanel(theme) {}

  UGuiWidget *HitTest(int x, int y) override
  {
    if (!visible_)
    {
      return nullptr;
    }
    std::vector<UGuiWidget *> sorted;
    sorted.reserve(children_.size());
    for (auto &child : children_)
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

class TouchHoldButton : public UGuiButton
{
public:
  TouchHoldButton(const GuiTheme *theme, std::string label, KeyCode key,
                  TouchInputBridge *bridge,
                  std::function<void()> onPress = nullptr)
      : UGuiButton(theme, std::move(label)), key_(key), bridge_(bridge),
        onPress_(std::move(onPress))
  {
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!UGuiButton::OnMouseDown(event))
    {
      return false;
    }
    capturePointerId_ = event.pointerId;
    if (onPress_)
    {
      onPress_();
    }
    held_ = true;
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, true);
    }
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!held_ || !GuiPointerMatches(event.pointerId, capturePointerId_))
    {
      return false;
    }
    const bool handled = UGuiButton::OnMouseUp(event);
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, false);
    }
    held_ = false;
    capturePointerId_ = -1;
    return handled;
  }

  void ForceRelease()
  {
    held_ = false;
    capturePointerId_ = -1;
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, false);
    }
  }

private:
  KeyCode key_;
  TouchInputBridge *bridge_;
  std::function<void()> onPress_;
  bool held_{false};
  int capturePointerId_{-1};
};

class TouchVirtualJoystick : public UGuiWidget
{
public:
  TouchVirtualJoystick(const GuiTheme *theme, TouchInputBridge *bridge)
      : theme_(theme), bridge_(bridge)
  {
  }

  bool IsActive() const { return active_; }

  void ForceRelease()
  {
    active_ = false;
    capturePointerId_ = -1;
    knobOffset_ = {0.f, 0.f};
    if (bridge_)
    {
      bridge_->SetJoystickVector({0.f, 0.f});
      bridge_->SetJoystickActive(false);
    }
  }

  bool OnCapturedMove(int pointerId, int x, int y)
  {
    if (!active_ || !GuiPointerMatches(pointerId, capturePointerId_))
    {
      return false;
    }
    UpdateStick(x, y);
    return true;
  }

  void Draw(UGuiRenderer &renderer) override
  {
    if (!visible_ || !theme_)
    {
      return;
    }
    renderer.DrawFilledRect(bounds_, {0.12f, 0.12f, 0.14f, 0.45f});
    renderer.DrawBorderRect(bounds_, {1.f, 1.f, 1.f, 0.35f},
                            theme_->borderThickness);
    renderer.DrawFilledRect(ComputeKnobRect(), {0.92f, 0.92f, 0.95f, 0.85f});
    renderer.DrawBorderRect(ComputeKnobRect(), {1.f, 1.f, 1.f, 0.5f}, 1);
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y) ||
        event.button != GuiMouseButton::Left)
    {
      return false;
    }
    active_ = true;
    capturePointerId_ = event.pointerId;
    if (bridge_)
    {
      bridge_->SetJoystickActive(true);
      bridge_->RequestCameraBaseline(static_cast<float>(event.x),
                                     static_cast<float>(event.y));
    }
    UpdateStick(event.x, event.y);
    return true;
  }

  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.pointerId, capturePointerId_))
    {
      return false;
    }
    UpdateStick(event.x, event.y);
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.pointerId, capturePointerId_))
    {
      return false;
    }
    ForceRelease();
    capturePointerId_ = -1;
    return event.button == GuiMouseButton::Left;
  }

private:
  void UpdateStick(int x, int y)
  {
    const float centerX = static_cast<float>(bounds_.x + bounds_.w / 2);
    const float centerY = static_cast<float>(bounds_.y + bounds_.h / 2);
    float dx = static_cast<float>(x) - centerX;
    float dy = centerY - static_cast<float>(y);
    const float maxRadius = static_cast<float>(bounds_.w) * 0.38f;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > maxRadius && length > 0.f)
    {
      dx = dx / length * maxRadius;
      dy = dy / length * maxRadius;
    }
    knobOffset_ = {dx, -dy};
    if (bridge_)
    {
      const glm::vec2 normalized =
          maxRadius > 0.f ? glm::vec2(dx / maxRadius, dy / maxRadius)
                          : glm::vec2{0.f, 0.f};
      bridge_->SetJoystickVector(normalized);
    }
  }

  GuiRect ComputeKnobRect() const
  {
    const int knobSize = std::max(28, bounds_.w / 3);
    const float centerX =
        static_cast<float>(bounds_.x + bounds_.w / 2) + knobOffset_.x;
    const float centerY =
        static_cast<float>(bounds_.y + bounds_.h / 2) + knobOffset_.y;
    return {static_cast<int>(centerX - knobSize / 2),
            static_cast<int>(centerY - knobSize / 2), knobSize, knobSize};
  }

  const GuiTheme *theme_{nullptr};
  TouchInputBridge *bridge_{nullptr};
  glm::vec2 knobOffset_{0.f, 0.f};
  bool active_{false};
  int capturePointerId_{-1};
};

class TouchLookPad : public UGuiWidget
{
public:
  TouchLookPad(const GuiTheme *theme, const float *uiScale,
               TouchInputBridge *bridge)
      : theme_(theme), uiScale_(uiScale), bridge_(bridge)
  {
  }

  UGuiWidget *HitTest(int x, int y) override
  {
    if (!visible_ || !enabled_ || !bounds_.Contains(x, y))
    {
      return nullptr;
    }
    return this;
  }

  bool OnCapturedMove(int pointerId, int x, int y)
  {
    if (!active_ || !GuiPointerMatches(pointerId, capturePointerId_))
    {
      return false;
    }
    return OnLookMove(x, y);
  }

  void Draw(UGuiRenderer &renderer) override
  {
    if (!visible_ || !theme_)
    {
      return;
    }
    const float scale = uiScale_ ? *uiScale_ : 1.f;
    renderer.DrawFilledRect(bounds_, {0.10f, 0.12f, 0.16f, 0.35f});
    renderer.DrawBorderRect(bounds_, {1.f, 1.f, 1.f, 0.28f},
                            theme_->borderThickness);
    GuiRect labelRect = bounds_;
    labelRect.y += bounds_.h / 2 - ScalePx(12, scale);
    labelRect.h = ScalePx(24, scale);
    renderer.DrawTextCenteredInRect(labelRect, "Look", {0.92f, 0.92f, 0.95f});
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y) ||
        event.button != GuiMouseButton::Left)
    {
      return false;
    }
    active_ = true;
    capturePointerId_ = event.pointerId;
    dragged_ = false;
    startX_ = event.x;
    startY_ = event.y;
    lastX_ = event.x;
    lastY_ = event.y;
    downTime_ = std::chrono::steady_clock::now();
    if (bridge_)
    {
      bridge_->RequestCameraBaseline(static_cast<float>(event.x),
                                     static_cast<float>(event.y));
    }
    return true;
  }

  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.pointerId, capturePointerId_))
    {
      return false;
    }
    return OnLookMove(event.x, event.y);
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!active_ || !GuiPointerMatches(event.pointerId, capturePointerId_))
    {
      return false;
    }
    if (bridge_ && !dragged_)
    {
      const float holdSeconds = std::chrono::duration<float>(
                                    std::chrono::steady_clock::now() - downTime_)
                                    .count();
      const float dragDistance = std::sqrt(
          static_cast<float>((event.x - startX_) * (event.x - startX_) +
                             (event.y - startY_) * (event.y - startY_)));
      const float slop = uiScale_ ? std::min(36.f * *uiScale_, 56.f) : 36.f;
      if (dragDistance < slop && holdSeconds < 0.50f)
      {
        bridge_->QueuePlaceTap(static_cast<float>(event.x),
                               static_cast<float>(event.y));
      }
    }
    active_ = false;
    dragged_ = false;
    capturePointerId_ = -1;
    return true;
  }

private:
  bool OnLookMove(int x, int y)
  {
    if (!active_ || !bridge_)
    {
      return false;
    }
    const float dx = static_cast<float>(x - lastX_);
    const float dy = static_cast<float>(y - lastY_);
    const float dragDistance = std::sqrt(
        static_cast<float>((x - startX_) * (x - startX_) +
                           (y - startY_) * (y - startY_)));
    const float threshold = uiScale_ ? 10.f * *uiScale_ : 10.f;
    if (!dragged_ && dragDistance > threshold)
    {
      dragged_ = true;
      bridge_->RequestCameraBaseline(static_cast<float>(x), static_cast<float>(y));
    }
    if (dragged_)
    {
      bridge_->AddLookDelta(dx, dy);
    }
    lastX_ = x;
    lastY_ = y;
    return true;
  }

  const GuiTheme *theme_{nullptr};
  const float *uiScale_{nullptr};
  TouchInputBridge *bridge_{nullptr};
  int startX_{0};
  int startY_{0};
  int lastX_{0};
  int lastY_{0};
  bool active_{false};
  bool dragged_{false};
  int capturePointerId_{-1};
  std::chrono::steady_clock::time_point downTime_{};
};

} // namespace

GuiTouchControls::GuiTouchControls(const GuiTheme *theme,
                                   TouchInputBridge *bridge,
                                   std::function<void()> onMenu,
                                   std::function<void()> onInventory,
                                   std::function<void()> onConsole,
                                   std::function<void()> onJumpPress)
    : theme_(theme), bridge_(bridge), onMenu_(std::move(onMenu)),
      onInventory_(std::move(onInventory)),
      onConsole_(std::move(onConsole)),
      onJumpPress_(std::move(onJumpPress))
{
}

GuiTouchControls::~GuiTouchControls() = default;

void GuiTouchControls::Build(UGuiPanel *parent)
{
  if (!parent || !theme_ || !bridge_)
  {
    return;
  }
  auto panel = std::make_unique<TouchControlPanel>(theme_);
  panel->SetDrawBackground(false);
  panel->SetZOrder(kTouchControlsZOrder);
  root_ = panel.get();
  parent->AddChild(std::move(panel));

  auto joystick =
      std::make_unique<TouchVirtualJoystick>(theme_, bridge_);
  auto *joystickWidget = joystick.get();
  joystick_ = joystickWidget;
  root_->AddChild(std::move(joystick));

  auto jump = std::make_unique<TouchHoldButton>(
      theme_, "Jump", KeyCode::Key_Space, bridge_,
      [this]() {
        if (onJumpPress_)
        {
          onJumpPress_();
        }
      });
  auto *jumpWidget = jump.get();
  jump->SetZOrder(kTouchControlsZOrder + 2);
  jumpButton_ = jumpWidget;
  root_->AddChild(std::move(jump));

  auto sneak = std::make_unique<TouchHoldButton>(theme_, "Sneak", KeyCode::Key_Shift,
                                                 bridge_);
  auto *sneakWidget = sneak.get();
  sneak->SetZOrder(kTouchControlsZOrder + 2);
  sneakButton_ = sneakWidget;
  root_->AddChild(std::move(sneak));

  auto inventory = std::make_unique<UGuiButton>(theme_, "Inv");
  inventory->SetOnClick([this]() {
    if (onInventory_)
    {
      onInventory_();
    }
  });
  inventoryButton_ = inventory.get();
  root_->AddChild(std::move(inventory));

  auto menu = std::make_unique<UGuiButton>(theme_, "Menu");
  menu->SetOnClick([this]() {
    if (onMenu_)
    {
      onMenu_();
    }
  });
  menuButton_ = menu.get();
  root_->AddChild(std::move(menu));

  auto console = std::make_unique<UGuiButton>(theme_, "Cmd");
  console->SetOnClick([this]() {
    if (onConsole_)
    {
      onConsole_();
    }
  });
  consoleButton_ = console.get();
  root_->AddChild(std::move(console));

  auto lookPad = std::make_unique<TouchLookPad>(theme_, &uiScale_, bridge_);
  lookPad->SetZOrder(kTouchControlsZOrder + 1);
  lookPad_ = lookPad.get();
  root_->AddChild(std::move(lookPad));

  routeCapturedMove_ = [joystickWidget, lookPad = lookPad_](int pointerId, int x,
                                                            int y)
  {
    if (static_cast<TouchVirtualJoystick *>(joystickWidget)
            ->OnCapturedMove(pointerId, x, y))
    {
      return true;
    }
    if (lookPad)
    {
      return static_cast<TouchLookPad *>(lookPad)->OnCapturedMove(pointerId, x,
                                                                  y);
    }
    return false;
  };
  releaseJoystickCapture_ = [joystickWidget]()
  { joystickWidget->ForceRelease(); };
  releaseAllCaptures_ = [joystickWidget, jumpWidget, sneakWidget]()
  {
    joystickWidget->ForceRelease();
    jumpWidget->ForceRelease();
    sneakWidget->ForceRelease();
  };
}

bool GuiTouchControls::RouteCapturedMove(int pointerId, int x, int y)
{
  return routeCapturedMove_ ? routeCapturedMove_(pointerId, x, y) : false;
}

void GuiTouchControls::ReleaseJoystickCapture()
{
  if (releaseJoystickCapture_)
  {
    releaseJoystickCapture_();
  }
  if (bridge_)
  {
    bridge_->ResetJoystick();
  }
}

void GuiTouchControls::ReleaseAllCaptures()
{
  if (releaseAllCaptures_)
  {
    releaseAllCaptures_();
  }
  if (bridge_)
  {
    bridge_->ResetJoystick();
  }
}

void GuiTouchControls::Layout(int width, int height, int offsetX, int offsetY,
                              float uiScale)
{
  if (!root_)
  {
    return;
  }
  uiScale_ = uiScale;
  root_->SetBounds({0, 0, width, height});

  const int buttonSize = ScalePx(kButtonSizeBase, uiScale);
  const int margin = std::max(ScalePx(24, uiScale),
                              ScalePx(kMarginBase, uiScale));
  const int shortEdge = std::min(width, height);
  const int joystickTarget = ScalePx(kJoystickSizeBase, uiScale);
  const int joystickMax =
      std::max(ScalePx(120, uiScale), static_cast<int>(shortEdge * 0.20f));
  const int joystickSize = std::min(joystickTarget, joystickMax);
  const int buttonGap = ScalePx(8, uiScale);
  const int controlLift = ScalePx(8, uiScale);

  const int bottomRowY = height - buttonSize - margin;
  const int joystickY = bottomRowY - joystickSize - controlLift;
  const int leftMargin =
      std::max(margin, offsetX + ScalePx(12, uiScale));
  const int leftControlsW = leftMargin + joystickSize + margin;
  const int leftControlsTop = std::max(0, joystickY - controlLift);
  const int leftControlsH = std::max(1, height - leftControlsTop);
  const int rightColX = width - margin - buttonSize;
  const int jumpX = rightColX - buttonSize - buttonGap;
  const int topRightY = margin + ScalePx(kTopRightStackOffsetBase, uiScale);
  const int topRightStackH =
      buttonSize * 3 + buttonGap * 2 + margin;
  if (joystick_)
  {
    joystick_->SetBounds({leftMargin, joystickY, joystickSize, joystickSize});
  }
  if (jumpButton_)
  {
    jumpButton_->SetBounds({jumpX, bottomRowY, buttonSize, buttonSize});
  }
  if (sneakButton_)
  {
    sneakButton_->SetBounds({rightColX, bottomRowY, buttonSize, buttonSize});
  }
  if (menuButton_)
  {
    menuButton_->SetBounds(
        {width - buttonSize - margin, topRightY, buttonSize, buttonSize});
  }
  if (inventoryButton_)
  {
    inventoryButton_->SetBounds({width - buttonSize - margin,
                                   topRightY + buttonSize + buttonGap,
                                   buttonSize, buttonSize});
  }
  if (consoleButton_)
  {
    consoleButton_->SetBounds(
        {width - buttonSize - margin,
         topRightY + (buttonSize + buttonGap) * 2, buttonSize, buttonSize});
  }

  if (lookPad_)
  {
    const int lookSize = std::max(joystickSize, buttonSize * 2 + buttonGap);
    const int lookX = width - margin - lookSize;
    const int lookY = bottomRowY - lookSize - controlLift;
    lookPad_->SetBounds({lookX, lookY, lookSize, lookSize});
  }

  if (bridge_)
  {
    bridge_->ClearBlockedGameRegions();
    bridge_->SetBlockedGameRegion(
        0, {offsetX + leftMargin, offsetY + leftControlsTop,
            leftControlsW, leftControlsH});
    bridge_->SetBlockedGameRegion(
        1, {offsetX + std::max(0, width - buttonSize - margin * 2),
            offsetY + topRightY - margin / 2, buttonSize + margin * 2,
            topRightStackH});
    const int actionPad = ScalePx(4, uiScale);
    bridge_->SetBlockedGameRegion(
        2, {offsetX + std::max(0, jumpX - actionPad),
            offsetY + std::max(0, bottomRowY - actionPad),
            buttonSize * 2 + buttonGap + actionPad * 2,
            buttonSize + actionPad * 2});
  }
}

} // namespace cutum
