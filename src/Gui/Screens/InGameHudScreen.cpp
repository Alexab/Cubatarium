#include "Gui/Screens/InGameHudScreen.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWidget.h"

#if defined(__ANDROID__)
#include "Gui/Widgets/GuiTouchControls.h"
#endif

namespace cutum
{

UInGameHudScreen::UInGameHudScreen(UGameSession *session, const GuiTheme *theme,
                                   IUGuiIconSource *icons)
    : Session(session), Theme(theme), Icons(icons)
{
}

UInGameHudScreen::~UInGameHudScreen() = default;

bool UInGameHudScreen::PickSlot(int x, int y, SlotAddress &out)
{
  if (!RootPanel || !Theme)
  {
    return false;
  }
  EnsureHotbarWidgets();
  LayoutHotbar();

  UGuiWidget *hit = RootPanel->HitTest(x, y);
  if (!hit)
  {
    return false;
  }
  for (size_t i = 0; i < PrimarySlots.size(); ++i)
  {
    if (PrimarySlots[i] == hit)
    {
      out.surface = SlotSurface::Hotbar;
      out.bar = 0;
      out.slot = i;
      return true;
    }
  }
  for (size_t i = 0; i < SecondarySlots.size(); ++i)
  {
    UGuiSlot *slot = SecondarySlots[i];
    if (slot && slot == hit && slot->IsVisible())
    {
      out.surface = SlotSurface::Hotbar;
      out.bar = 1;
      out.slot = i;
      return true;
    }
  }
  return false;
}

void UInGameHudScreen::Build(UGuiContext &ctx)
{
  Renderer = &ctx.GetRenderer();
  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(false);
  RootPanel = panel.get();
  Root = std::move(panel);
  HotbarBuilt = false;
#if defined(__ANDROID__)
  if (TouchControls)
  {
    TouchControls->Build(RootPanel);
  }
#endif
}

#if defined(__ANDROID__)
void UInGameHudScreen::ConfigureTouchControls(UTouchInputBridge *bridge,
                                              std::function<void()> onMenu,
                                              std::function<void()> onInventory,
                                              std::function<void()> onConsole,
                                              std::function<void()> onJumpPress)
{
  TouchControls = std::make_unique<UGuiTouchControls>(
      Theme, bridge, std::move(onMenu), std::move(onInventory),
      std::move(onConsole), std::move(onJumpPress));
}

bool UInGameHudScreen::RouteTouchMove(int PointerId, int x, int y)
{
  return TouchControls && TouchControls->RouteCapturedMove(PointerId, x, y);
}

void UInGameHudScreen::ReleaseJoystickCapture()
{
  if (TouchControls)
  {
    TouchControls->ReleaseJoystickCapture();
  }
}

void UInGameHudScreen::ReleaseJoystickCaptureForPointer(int pointer_id)
{
  if (TouchControls)
  {
    TouchControls->ReleaseJoystickCaptureForPointer(pointer_id);
  }
}

bool UInGameHudScreen::HitTestTouchControls(int x, int y) const
{
  return TouchControls && TouchControls->HitTestTopRightReserved(x, y);
}

void UInGameHudScreen::RenderTouchControlsOverlay(UGuiContext &ctx, int width,
                                                  int height)
{
  if (!TouchControls)
  {
    return;
  }
  UGuiRenderer &renderer = ctx.GetRenderer();
  renderer.BeginFrame(width, height);
  TouchControls->RenderOverlay(renderer);
  renderer.EndFrame();
}

void UInGameHudScreen::ReleaseTouchCaptures()
{
  if (TouchControls)
  {
    TouchControls->ReleaseAllCaptures();
  }
}
#endif

void UInGameHudScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  LayoutHotbar();
#if defined(__ANDROID__)
  if (TouchControls)
  {
    TouchControls->Layout(ViewportW, ViewportH, GetContentOffsetX(),
                          GetContentOffsetY());
  }
#endif
}

void UInGameHudScreen::SetPointerPosition(int x, int y)
{
  PointerX = x;
  PointerY = y;
}

void UInGameHudScreen::EnsureHotbarWidgets()
{
  if (HotbarBuilt || !RootPanel || !Session || !Theme)
  {
    return;
  }

  const int slotSize = Theme->HotbarSlotSize;

  for (size_t i = 0; i < 10; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    const size_t index = i;
    SlotAddress address;
    address.surface = SlotSurface::Hotbar;
    address.bar = 0;
    address.slot = index;
    slot->SetOnClick(
        [this, index]()
        {
          if (!Session->ApplyPendingAssignment(0, index))
          {
            Session->SelectSlot(0, index);
          }
        });
    slot->SetOnBeginDrag(
        [this, address]()
        {
          const InventoryEntryRef entry =
              Session->GetHotbarEntryRef(address.bar, address.slot);
          if (!entry.empty)
          {
            Session->BeginDragFromSlot(address, entry);
          }
        });
    const int hotkeyNumber = (index < 9) ? static_cast<int>(index + 1) : 0;
    slot->SetCornerHint(std::to_string(hotkeyNumber));
    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(RootPanel->AddChild(std::move(slot)));
    PrimarySlots.push_back(ptr);
  }
  for (size_t i = 0; i < 10; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    const size_t index = i;
    SlotAddress address;
    address.surface = SlotSurface::Hotbar;
    address.bar = 1;
    address.slot = index;
    slot->SetOnClick(
        [this, index]()
        {
          if (!Session->ApplyPendingAssignment(1, index))
          {
            Session->SelectSlot(1, index);
          }
        });
    slot->SetOnBeginDrag(
        [this, address]()
        {
          const InventoryEntryRef entry =
              Session->GetHotbarEntryRef(address.bar, address.slot);
          if (!entry.empty)
          {
            Session->BeginDragFromSlot(address, entry);
          }
        });
    UGuiSlot *ptr =
        static_cast<UGuiSlot *>(RootPanel->AddChild(std::move(slot)));
    SecondarySlots.push_back(ptr);
  }

  auto tip = std::make_unique<UGuiLabel>(Theme, "");
  tip->SetTextAlign(GuiTextAlign::Center);
  tip->SetDrawBackground(true);
  tip->SetVisible(false);
  Tooltip = tip.get();
  RootPanel->AddChild(std::move(tip));

  HotbarBuilt = true;
  LayoutHotbar();
}

void UInGameHudScreen::LayoutHotbar()
{
  if (!HotbarBuilt || !Theme)
  {
    return;
  }
  RootPanel->SetBounds(
      {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});

  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int totalW = static_cast<int>(PrimarySlots.size()) * slotSize +
                     (static_cast<int>(PrimarySlots.size()) - 1) * gap;
  const int startX = (ViewportW - totalW) / 2;
  const int rowY = ViewportH - Theme->HotbarMarginBottom - slotSize;

  int x = startX;
  for (UGuiSlot *slot : PrimarySlots)
  {
    if (slot)
    {
      slot->SetBounds({x, rowY, slotSize, slotSize});
      x += slotSize + gap;
    }
  }

  const bool showSecondary = Session->GetBarCount() > 1;
  const int secX = ViewportW - Theme->HotbarSecondaryMarginRight - slotSize;
  int secY = ViewportH - Theme->HotbarSecondaryMarginBottom - slotSize;
  for (UGuiSlot *slot : SecondarySlots)
  {
    if (!slot)
    {
      continue;
    }
    if (showSecondary)
    {
      slot->SetVisible(true);
      slot->SetBounds({secX, secY, slotSize, slotSize});
      secY -= slotSize + gap;
    }
    else
    {
      slot->SetVisible(false);
    }
  }

  if (Tooltip)
  {
    Tooltip->SetVisible(false);
  }
}

void UInGameHudScreen::UpdateSlotData()
{
  if (!Session)
  {
    return;
  }
  const auto primary = Session->GetBarSlots(0);
  for (size_t i = 0; i < PrimarySlots.size() && i < primary.size(); ++i)
  {
    PrimarySlots[i]->SetLabel(primary[i].label);
    PrimarySlots[i]->SetSelected(primary[i].selected);
  }
  const auto secondary = Session->GetBarSlots(1);
  for (size_t i = 0; i < SecondarySlots.size() && i < secondary.size(); ++i)
  {
    SecondarySlots[i]->SetLabel(secondary[i].label);
    SecondarySlots[i]->SetSelected(secondary[i].selected);
  }
}

void UInGameHudScreen::SyncSlotIcons()
{
  if (!Session || !Icons)
  {
    return;
  }
  const auto primary = Session->GetBarSlots(0);
  for (size_t i = 0; i < PrimarySlots.size() && i < primary.size(); ++i)
  {
    GLuint tex = 0;
    if (!primary[i].Id.empty())
    {
      switch (primary[i].entryKind)
      {
      case InventoryEntryKind::Block:
        tex = Icons->GetBlockIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::Object:
        tex = Icons->GetObjectIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::UCreature:
        tex = Icons->GetCreatureIconTexture(primary[i].Id);
        break;
      case InventoryEntryKind::Skin:
        tex = Icons->GetSkinIconTexture(primary[i].Id);
        break;
      }
    }
    PrimarySlots[i]->SetIconTexture(tex);
  }
  const auto secondary = Session->GetBarSlots(1);
  for (size_t i = 0; i < SecondarySlots.size() && i < secondary.size(); ++i)
  {
    GLuint tex = 0;
    if (!secondary[i].Id.empty())
    {
      switch (secondary[i].entryKind)
      {
      case InventoryEntryKind::Block:
        tex = Icons->GetBlockIconTexture(secondary[i].Id);
        break;
      case InventoryEntryKind::Object:
        tex = Icons->GetObjectIconTexture(secondary[i].Id);
        break;
      case InventoryEntryKind::UCreature:
        tex = Icons->GetCreatureIconTexture(secondary[i].Id);
        break;
      case InventoryEntryKind::Skin:
        tex = Icons->GetSkinIconTexture(secondary[i].Id);
        break;
      }
    }
    SecondarySlots[i]->SetIconTexture(tex);
  }
}

void UInGameHudScreen::UpdateTooltips()
{
  if (!Session || !Tooltip || !Theme)
  {
    return;
  }

  const GuiRect viewport{GetContentOffsetX(), GetContentOffsetY(), ViewportW,
                         ViewportH};
  const auto primary = Session->GetBarSlots(0);
  const auto secondary = Session->GetBarSlots(1);

  auto positionTip = [&](const std::string &text, int tipX, int tipY)
  {
    if (text.empty())
    {
      Tooltip->SetVisible(false);
      return;
    }
    const int textW =
        MeasureTooltipTextWidth(text, *Theme, Renderer);
    LayoutTooltipNearPointer(*Tooltip, text, tipX, tipY, viewport, *Theme,
                             textW);
  };

  if (PointerX >= 0 && PointerY >= 0 && RootPanel)
  {
    if (UGuiWidget *hit = RootPanel->HitTest(PointerX, PointerY))
    {
      for (size_t i = 0; i < PrimarySlots.size(); ++i)
      {
        if (hit == PrimarySlots[i] && i < primary.size() &&
            !primary[i].label.empty())
        {
          positionTip(primary[i].label, PointerX, PointerY);
          return;
        }
      }
      for (size_t i = 0; i < SecondarySlots.size(); ++i)
      {
        if (hit == SecondarySlots[i] && i < secondary.size() &&
            !secondary[i].label.empty())
        {
          positionTip(secondary[i].label, PointerX, PointerY);
          return;
        }
      }
    }
  }

  const size_t activePrimary = Session->GetSelectedSlot(0);
  if (activePrimary < primary.size() && !primary[activePrimary].label.empty())
  {
    UGuiSlot *slot = PrimarySlots[activePrimary];
    const GuiRect b = slot ? slot->GetBounds() : GuiRect{};
    positionTip(primary[activePrimary].label, b.X + b.W / 2, b.Y + b.H / 2);
    return;
  }
  const size_t activeSecondary = Session->GetSelectedSlot(1);
  if (activeSecondary < secondary.size() &&
      !secondary[activeSecondary].label.empty())
  {
    UGuiSlot *slot = SecondarySlots[activeSecondary];
    const GuiRect b = slot ? slot->GetBounds() : GuiRect{};
    positionTip(secondary[activeSecondary].label, b.X + b.W / 2,
                b.Y + b.H / 2);
    return;
  }

  Tooltip->SetVisible(false);
}

void UInGameHudScreen::Update(double /*dt*/)
{
  if (!RootPanel || !Session || !Theme)
  {
    return;
  }
  EnsureHotbarWidgets();
  LayoutHotbar();
  UpdateSlotData();
  UpdateTooltips();
}

} // namespace cutum
