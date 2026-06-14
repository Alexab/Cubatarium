#include "Gui/Widgets/GuiTouchControls.h"

#include "App/Platform/InputManager.h"
#include "App/Platform/TouchInputBridge.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiPanel.h"

namespace cutum
{

namespace
{

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
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, true);
    }
    return UGuiButton::OnMouseDown(event);
  }

  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    if (bridge_)
    {
      bridge_->SetHeldKey(key_, false);
    }
    return UGuiButton::OnMouseUp(event);
  }

private:
  KeyCode key_;
  TouchInputBridge *bridge_;
};

constexpr int kButtonSize = 72;
constexpr int kMargin = 16;

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
  auto panel = std::make_unique<UGuiPanel>(theme_);
  panel->SetDrawBackground(false);
  root_ = panel.get();
  parent->AddChild(std::move(panel));

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
}

void GuiTouchControls::Layout(int width, int height)
{
  if (!root_)
  {
    return;
  }
  root_->SetBounds({0, 0, width, height});

  const int jumpY = height - kButtonSize - kMargin;
  if (jumpButton_)
  {
    jumpButton_->SetBounds({kMargin, jumpY, kButtonSize, kButtonSize});
  }
  if (sneakButton_)
  {
    sneakButton_->SetBounds(
        {kMargin + kButtonSize + 8, jumpY, kButtonSize, kButtonSize});
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
}

} // namespace cutum
