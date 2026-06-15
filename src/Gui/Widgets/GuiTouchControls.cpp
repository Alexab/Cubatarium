#include "Gui/Widgets/GuiTouchControls.h"

#include "App/Platform/InputManager.h"
#include "App/Platform/TouchInputBridge.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiPanel.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

namespace
{

constexpr int kButtonSize = 64;
constexpr int kMargin = 16;
constexpr int kJoystickSize = 148;
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
                  TouchInputBridge *bridge)
      : UGuiButton(theme, std::move(label)), key_(key), bridge_(bridge)
  {
  }

  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    if (!UGuiButton::OnMouseDown(event))
    {
      return false;
    }
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, true);
    }
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    const bool handled = UGuiButton::OnMouseUp(event);
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, false);
    }
    return handled;
  }

private:
  KeyCode key_;
  TouchInputBridge *bridge_;
};

class TouchVirtualJoystick : public UGuiWidget
{
public:
  TouchVirtualJoystick(const GuiTheme *theme, TouchInputBridge *bridge)
      : theme_(theme), bridge_(bridge)
  {
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
    UpdateStick(event.x, event.y);
    return true;
  }

  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    if (!active_)
    {
      return false;
    }
    UpdateStick(event.x, event.y);
    return true;
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (!active_)
    {
      return false;
    }
    active_ = false;
    knobOffset_ = {0.f, 0.f};
    if (bridge_)
    {
      bridge_->SetJoystickVector({0.f, 0.f});
    }
    return event.button == GuiMouseButton::Left;
  }

private:
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

  const GuiTheme *theme_{nullptr};
  TouchInputBridge *bridge_{nullptr};
  glm::vec2 knobOffset_{0.f, 0.f};
  bool active_{false};
};

class TouchLookPad : public UGuiWidget
{
public:
  explicit TouchLookPad(const GuiTheme *theme) : theme_(theme) {}

  UGuiWidget *HitTest(int x, int y) override
  {
    (void)x;
    (void)y;
    return nullptr;
  }

  void Draw(UGuiRenderer &renderer) override
  {
    if (!visible_ || !theme_)
    {
      return;
    }
    renderer.DrawFilledRect(bounds_, {0.10f, 0.12f, 0.16f, 0.22f});
    renderer.DrawBorderRect(bounds_, {1.f, 1.f, 1.f, 0.22f},
                            theme_->borderThickness);
    GuiRect labelRect = bounds_;
    labelRect.y += bounds_.h / 2 - 12;
    labelRect.h = 24;
    renderer.DrawTextCenteredInRect(labelRect, "Look", {0.92f, 0.92f, 0.95f});
  }

private:
  const GuiTheme *theme_{nullptr};
};

} // namespace

GuiTouchControls::GuiTouchControls(const GuiTheme *theme,
                                   TouchInputBridge *bridge,
                                   std::function<void()> onMenu,
                                   std::function<void()> onInventory)
    : theme_(theme), bridge_(bridge), onMenu_(std::move(onMenu)),
      onInventory_(std::move(onInventory))
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
  joystick_ = joystick.get();
  root_->AddChild(std::move(joystick));

  auto jump = std::make_unique<TouchHoldButton>(theme_, "Jump", KeyCode::Key_Space,
                                                bridge_);
  jumpButton_ = jump.get();
  root_->AddChild(std::move(jump));

  auto sneak = std::make_unique<TouchHoldButton>(theme_, "Sneak", KeyCode::Key_Shift,
                                                 bridge_);
  sneakButton_ = sneak.get();
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

  auto lookPad = std::make_unique<TouchLookPad>(theme_);
  lookPad_ = lookPad.get();
  root_->AddChild(std::move(lookPad));
}

void GuiTouchControls::Layout(int width, int height, int offsetX, int offsetY)
{
  if (!root_)
  {
    return;
  }
  root_->SetBounds({0, 0, width, height});

  const int bottomRowY = height - kButtonSize - kMargin;
  const int leftControlsW =
      kMargin + kJoystickSize + 12 + kButtonSize + 8 + kButtonSize + kMargin;
  const int leftControlsH = kJoystickSize + kMargin;
  if (joystick_)
  {
    joystick_->SetBounds(
        {kMargin, bottomRowY - kJoystickSize + kButtonSize, kJoystickSize,
         kJoystickSize});
  }
  if (jumpButton_)
  {
    const int jumpX = kMargin + kJoystickSize + 12;
    jumpButton_->SetBounds({jumpX, bottomRowY, kButtonSize, kButtonSize});
  }
  if (sneakButton_)
  {
    const int jumpX = kMargin + kJoystickSize + 12;
    sneakButton_->SetBounds(
        {jumpX + kButtonSize + 8, bottomRowY, kButtonSize, kButtonSize});
  }
  if (menuButton_)
  {
    menuButton_->SetBounds(
        {width - kButtonSize - kMargin, kMargin, kButtonSize, kButtonSize});
  }
  if (inventoryButton_)
  {
    inventoryButton_->SetBounds({width - kButtonSize - kMargin,
                                 kMargin + kButtonSize + 8, kButtonSize,
                                 kButtonSize});
  }

  constexpr int kHotbarReserve = 96;
  constexpr int kTopMenuReserve = 152;
  const int lookPadX =
      kMargin + static_cast<int>(static_cast<float>(width) * 0.58f);
  const int lookPadW = std::max(0, width - lookPadX - kMargin);
  const int lookPadY = kTopMenuReserve;
  const int lookPadH = std::max(0, height - lookPadY - kHotbarReserve);
  if (lookPad_)
  {
    lookPad_->SetBounds({lookPadX, lookPadY, lookPadW, lookPadH});
  }

  if (bridge_)
  {
    bridge_->ClearBlockedGameRegions();
    bridge_->SetBlockedGameRegion(
        0, {offsetX + kMargin,
            offsetY + std::max(0, height - leftControlsH),
            leftControlsW, leftControlsH});
    bridge_->SetBlockedGameRegion(
        1, {offsetX + std::max(0, width - kButtonSize - kMargin * 2),
            offsetY + kMargin, kButtonSize + kMargin * 2,
            kButtonSize * 2 + 8 + kMargin});
  }
}

} // namespace cutum
