#include "Gui/Screens/InGameHudScreen.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Game/WorldGameMode.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Version.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

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
      out.bar = 1 + i / 10;
      out.slot = i % 10;
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
void UInGameHudScreen::ConfigureTouchControls(
    UTouchInputBridge *bridge, std::function<void()> onMenu,
    std::function<void()> onInventory, std::function<void()> onConsole,
    std::function<void()> onJumpPress, TouchIsoControlCallbacks isoCallbacks)
{
  TouchControls = std::make_unique<UGuiTouchControls>(
      Theme, bridge, std::move(onMenu), std::move(onInventory),
      std::move(onConsole), std::move(onJumpPress), std::move(isoCallbacks));
}

void UInGameHudScreen::InvalidateTouchControlsLayout()
{
  if (TouchControls)
  {
    TouchControls->InvalidateLayout();
  }
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
  for (size_t i = 0; i < 30; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    const size_t index = i % 10, bar = 1 + i / 10;
    SlotAddress address;
    address.surface = SlotSurface::Hotbar;
    address.bar = bar;
    address.slot = index;
    slot->SetOnClick(
        [this, index, bar]()
        {
          if (!Session->ApplyPendingAssignment(bar, index))
          {
            Session->SelectSlot(bar, index);
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

  auto divider = std::make_unique<UGuiPanel>(Theme);
  divider->SetDrawBackground(true);
  HotbarDivider = divider.get();
  RootPanel->AddChild(std::move(divider));

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
  const int sectionGap = gap * 3 + std::max(2, Theme->BorderThickness * 2);
  const int slotCount = static_cast<int>(PrimarySlots.size());
  const int totalW = slotCount * slotSize + std::max(0, slotCount - 1) * gap +
                     (slotCount > 5 ? sectionGap - gap : 0);
  const int startX = (ViewportW - totalW) / 2;
  const int rowY = ViewportH - Theme->HotbarMarginBottom - slotSize;

  int x = startX;
  for (size_t i = 0; i < PrimarySlots.size(); ++i)
  {
    UGuiSlot *slot = PrimarySlots[i];
    if (slot)
    {
      slot->SetBounds({x, rowY, slotSize, slotSize});
      x += slotSize;
      if (i + 1 < PrimarySlots.size())
      {
        x += (i == 4) ? sectionGap : gap;
      }
    }
  }

  if (HotbarDivider && PrimarySlots.size() > 5 && PrimarySlots[4] &&
      PrimarySlots[5])
  {
    const GuiRect left = PrimarySlots[4]->GetBounds();
    const GuiRect right = PrimarySlots[5]->GetBounds();
    const int mid = (left.X + left.W + right.X) / 2;
    const int divW = std::max(2, Theme->BorderThickness * 2);
    const int divH = slotSize * 3 / 4;
    HotbarDivider->SetVisible(true);
    HotbarDivider->SetBounds(
        {mid - divW / 2, rowY + (slotSize - divH) / 2, divW, divH});
  }
  else if (HotbarDivider)
  {
    HotbarDivider->SetVisible(false);
  }

  for (size_t i = 0; i < SecondarySlots.size(); ++i)
  {
    auto *slot = SecondarySlots[i];
    if (!slot)
      continue;
    const size_t bar = 1 + i / 10;
    const int column = int(i / 10), row = int(i % 10);
    slot->SetVisible(bar < Session->GetBarCount());
    slot->SetBounds({ViewportW - Theme->HotbarSecondaryMarginRight - slotSize -
                         column * (slotSize + gap),
                     ViewportH - Theme->HotbarSecondaryMarginBottom - slotSize -
                         row * (slotSize + gap),
                     slotSize, slotSize});
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
  const auto firstSecondary = Session->GetBarSlots(1);
  std::vector<HotbarSlotView> secondary(firstSecondary.begin(),
                                        firstSecondary.end());
  for (size_t bar = 2; bar < 4; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    secondary.insert(secondary.end(), entries.begin(), entries.end());
  }
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
      case InventoryEntryKind::Item:
        tex = Icons->GetItemIconTexture(primary[i].Id);
        break;
      }
    }
    PrimarySlots[i]->SetIconTexture(tex);
    PrimarySlots[i]->SetWearProgress(
        primary[i].entryKind == InventoryEntryKind::Item ? primary[i].wear
                                                         : 0.f);
    PrimarySlots[i]->SetBroken(
        primary[i].entryKind == InventoryEntryKind::Item && primary[i].broken);
  }
  const auto firstSecondary = Session->GetBarSlots(1);
  std::vector<HotbarSlotView> secondary(firstSecondary.begin(),
                                        firstSecondary.end());
  for (size_t bar = 2; bar < 4; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    secondary.insert(secondary.end(), entries.begin(), entries.end());
  }
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
      case InventoryEntryKind::Item:
        tex = Icons->GetItemIconTexture(secondary[i].Id);
        break;
      }
    }
    SecondarySlots[i]->SetIconTexture(tex);
    SecondarySlots[i]->SetWearProgress(
        secondary[i].entryKind == InventoryEntryKind::Item ? secondary[i].wear
                                                           : 0.f);
    SecondarySlots[i]->SetBroken(secondary[i].entryKind ==
                                     InventoryEntryKind::Item &&
                                 secondary[i].broken);
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
  const auto firstSecondary = Session->GetBarSlots(1);
  std::vector<HotbarSlotView> secondary(firstSecondary.begin(),
                                        firstSecondary.end());
  for (size_t bar = 2; bar < 4; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    secondary.insert(secondary.end(), entries.begin(), entries.end());
  }

  auto positionTip = [&](const std::string &text, int tipX, int tipY)
  {
    if (text.empty())
    {
      Tooltip->SetVisible(false);
      return;
    }
    const int textW = MeasureTooltipTextWidth(text, *Theme, Renderer);
    LayoutTooltipNearPointer(*Tooltip, text, tipX, tipY, viewport, *Theme,
                             textW);
  };

  if (PointerX >= 0 && PointerY >= 0 && RootPanel)
  {
    if (UGuiWidget *hit = RootPanel->HitTest(PointerX, PointerY))
    {
      if (ModeBadge && hit == ModeBadge)
      {
        const bool survival =
            Session->GetWorldGameMode() == WorldGameMode::Survival;
        positionTip(survival ? "Survival — gather, craft, manage vitals"
                             : "Creative — unlimited blocks, fly, no vitals",
                    PointerX, PointerY);
        return;
      }
      for (size_t i = 0; i < PrimarySlots.size(); ++i)
      {
        if (hit == PrimarySlots[i] && i < primary.size() &&
            !primary[i].label.empty())
        {
          positionTip(FormatHotbarTooltip(primary[i]), PointerX, PointerY);
          return;
        }
      }
      for (size_t i = 0; i < SecondarySlots.size(); ++i)
      {
        if (hit == SecondarySlots[i] && i < secondary.size() &&
            !secondary[i].label.empty())
        {
          positionTip(FormatHotbarTooltip(secondary[i]), PointerX, PointerY);
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
    positionTip(FormatHotbarTooltip(primary[activePrimary]), b.X + b.W / 2,
                b.Y + b.H / 2);
    return;
  }
  const size_t activeSecondary = Session->GetSelectedSlot(1);
  if (activeSecondary < secondary.size() &&
      !secondary[activeSecondary].label.empty())
  {
    UGuiSlot *slot = SecondarySlots[activeSecondary];
    const GuiRect b = slot ? slot->GetBounds() : GuiRect{};
    positionTip(FormatHotbarTooltip(secondary[activeSecondary]), b.X + b.W / 2,
                b.Y + b.H / 2);
    return;
  }

  Tooltip->SetVisible(false);
}

std::string
UInGameHudScreen::FormatHotbarTooltip(const HotbarSlotView &slot) const
{
  if (slot.entryKind != InventoryEntryKind::Item || slot.Id.empty() || !Session)
  {
    return slot.label;
  }
  const auto world = Session->GetWorld();
  UItemDefinitionStorage *items =
      world ? world->GetItemDefinitionStorage() : nullptr;
  const ItemDefinition *def = items ? items->Get(slot.Id) : nullptr;
  return BuildItemTooltipText(slot.label, def, slot.wear, slot.broken);
}

void UInGameHudScreen::Update(double /*dt*/)
{
  if (!RootPanel || !Session || !Theme)
  {
    return;
  }
  EnsureHotbarWidgets();
  EnsureVitalWidgets();
  EnsureModeBadge();
  EnsureDebugInfoWidgets();
  LayoutHotbar();
  LayoutVitals();
  LayoutModeBadge();
  LayoutDebugInfo();
  UpdateDebugInfo();
  UpdateSlotData();
  UpdateVitalBars();
  UpdateModeBadge();
  UpdateTooltips();
#if defined(__ANDROID__)
  if (TouchControls)
  {
    TouchControls->Layout(ViewportW, ViewportH, GetContentOffsetX(),
                          GetContentOffsetY());
  }
#endif
}

void UInGameHudScreen::EnsureModeBadge()
{
  if (ModeBadgeBuilt || !RootPanel || !Theme)
  {
    return;
  }
  auto badge = std::make_unique<UGuiLabel>(Theme, "");
  badge->SetDrawBackground(true);
  badge->SetTextAlign(GuiTextAlign::Center);
  badge->SetVisible(true);
  ModeBadge = badge.get();
  RootPanel->AddChild(std::move(badge));
  auto hint = std::make_unique<UGuiLabel>(Theme, "");
  CarryHint = hint.get();
  hint->SetTextAlign(GuiTextAlign::Center);
  hint->SetDrawBackground(true);
  RootPanel->AddChild(std::move(hint));
  ModeBadgeBuilt = true;
}

void UInGameHudScreen::LayoutModeBadge()
{
  if (!ModeBadgeBuilt || !Theme || !ModeBadge)
  {
    return;
  }
  const int line = Theme->FontSizeBody + Theme->Padding;
  const int pad = Theme->Padding;
  const int w = std::max(Theme->FontSizeBody * 8, 88);
  const int x = GetContentOffsetX() + ViewportW - pad - w;
  const int y = pad + GetContentOffsetY();
  ModeBadge->SetBounds({x, y, w, line});
  if (CarryHint)
    CarryHint->SetBounds({GetContentOffsetX() + ViewportW / 6,
                          GetContentOffsetY() + ViewportH -
                              Theme->HotbarSlotSize -
                              Theme->HotbarMarginBottom - line * 2,
                          ViewportW * 2 / 3, line * 2});
}

void UInGameHudScreen::UpdateModeBadge()
{
  if (!ModeBadgeBuilt || !ModeBadge || !Session)
  {
    return;
  }
  const bool survival = Session->GetWorldGameMode() == WorldGameMode::Survival;
  ModeBadge->SetText(survival ? "Survival" : "Creative");
  ModeBadge->SetVisible(true);
  if (CarryHint)
  {
    auto world = Session->GetWorld();
    auto *actor = world ? world->GetControlledCreature() : nullptr;
    auto camera = world ? world->GetCurrentUserCamera() : nullptr;
    std::string text;
    if (actor && camera)
    {
      if (actor->CarryHandCount)
        text = std::to_string(actor->CarryHandCount) +
               " hand(s) occupied | Charge " +
               std::to_string(int(actor->ThrowCharge * 100)) + "% | " +
               GrabHintKey + ": place | Hold " + ThrowHintKey +
               ": throw | Esc: cancel";
      else if (actor->BowCharge > 0)
        text = "Bow charge " + std::to_string(int(actor->BowCharge * 100)) +
               "% | Release right mouse to fire";
      else if (auto id = world->PickCreatureByView(camera->GetPosition(),
                                                   camera->GetFront(), 2.6f))
        if (auto *target = world->GetCreature(*id))
        {
          auto profile = UWorldItemSystem::Instance().Profile(*world, *target);
          auto hands = CarryHands(
              profile,
              StatusEffectSystem::GetEffectiveAttributes(*actor).strength,
              actor->GetCollisionSize().y);
          text = hands ? (GrabHintKey + ": grab " + target->GetTypeId() + " (" +
                          std::to_string(hands) + " hands)")
                       : "Target is too heavy or too large";
        }
    }
    if (text.empty())
      text = UWorldItemSystem::Instance().GetFeedback();
    CarryHint->SetText(text);
    CarryHint->SetVisible(!text.empty());
  }
}

void UInGameHudScreen::EnsureDebugInfoWidgets()
{
  if (DebugInfoBuilt || !RootPanel || !Theme)
  {
    return;
  }
  auto makeBadge = [this](UGuiLabel *&out, bool alignRight)
  {
    auto lab = std::make_unique<UGuiLabel>(Theme, "");
    lab->SetInfoBadgeStyle(true, alignRight);
    out = lab.get();
    RootPanel->AddChild(std::move(lab));
  };
  makeBadge(WorldNameInfo, false);
  makeBadge(VersionInfo, false);
  makeBadge(CoordsInfo, true);
  VersionInfo->SetText(kCubatariumVersion);
  DebugInfoBuilt = true;
}

void UInGameHudScreen::LayoutDebugInfo()
{
  if (!DebugInfoBuilt || !Theme || !Session)
  {
    return;
  }
  const int line = Theme->FontSizeBody + Theme->Padding;
  const int pad = Theme->Padding;
  const int ox = GetContentOffsetX();
  const int oy = GetContentOffsetY();
  const int w = std::max(Theme->FontSizeBody * 24, 240);
  WorldNameInfo->SetBounds({ox + pad, oy + pad, w, line});
  VersionInfo->SetBounds({ox + pad, oy + ViewportH - pad - line, w, line});

  // Keep clear of the secondary hotbar column in the bottom-right corner.
  int right = ox + ViewportW - pad;
  if (Session->GetBarCount() > 1)
  {
    const int slotSize = Theme->HotbarSlotSize;
    right = ViewportW - Theme->HotbarSecondaryMarginRight - slotSize - pad;
  }
  CoordsInfo->SetBounds({right - w, oy + ViewportH - pad - line, w, line});
}

void UInGameHudScreen::UpdateDebugInfo()
{
  if (!DebugInfoBuilt || !Session)
  {
    return;
  }
  const auto world = Session->GetWorld();
  if (!world)
  {
    WorldNameInfo->SetVisible(false);
    CoordsInfo->SetVisible(false);
    return;
  }
  const std::string name =
      std::filesystem::u8path(world->GetWorldName()).filename().u8string();
  WorldNameInfo->SetText(name);
  WorldNameInfo->SetVisible(!name.empty());

  const auto camera = world->GetCurrentUserCamera();
  if (!camera)
  {
    CoordsInfo->SetVisible(false);
    return;
  }
  const glm::vec3 pos = camera->GetPosition();
  char buf[96];
  std::snprintf(
      buf, sizeof(buf), "X %d  Y %d  Z %d", static_cast<int>(std::floor(pos.x)),
      static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z)));
  CoordsInfo->SetText(buf);
  CoordsInfo->SetVisible(true);
}

void UInGameHudScreen::EnsureVitalWidgets()
{
  if (VitalsBuilt || !RootPanel || !Theme)
  {
    return;
  }
  auto makeLabel = [this](UGuiLabel *&out)
  {
    auto lab = std::make_unique<UGuiLabel>(Theme, "");
    lab->SetDrawBackground(true);
    lab->SetVisible(false);
    out = lab.get();
    RootPanel->AddChild(std::move(lab));
  };
  makeLabel(HealthLabel);
  makeLabel(SatietyLabel);
  makeLabel(ThirstLabel);
  makeLabel(FatigueLabel);
  makeLabel(BreathLabel);
  VitalsBuilt = true;
}

void UInGameHudScreen::LayoutVitals()
{
  if (!VitalsBuilt || !Theme)
  {
    return;
  }
  // Theme metrics already include UI scale — do not hardcode 20px rows
  // (FontSizeBody grows with scale and was taller than the bar on some
  // displays).
  const int line = Theme->FontSizeBody + Theme->Padding;
  const int gap = std::max(2, Theme->Padding / 4);
  const int pad = Theme->Padding;
  const int w = std::max(Theme->FontSizeBody * 12, 120);
  // Row 0 belongs to the world-name badge (LayoutDebugInfo).
  int y = pad + GetContentOffsetY() + line + gap;
  const int x = pad + GetContentOffsetX();
  UGuiLabel *labels[] = {HealthLabel, SatietyLabel, ThirstLabel, FatigueLabel,
                         BreathLabel};
  for (UGuiLabel *lab : labels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({x, y, w, line});
    y += line + gap;
  }
}

void UInGameHudScreen::UpdateVitalBars()
{
  if (!VitalsBuilt || !Session)
  {
    return;
  }
  const bool survival = Session->GetWorldGameMode() == WorldGameMode::Survival;
  UGuiLabel *labels[] = {HealthLabel, SatietyLabel, ThirstLabel, FatigueLabel,
                         BreathLabel};
  if (!survival)
  {
    for (UGuiLabel *lab : labels)
    {
      if (lab)
      {
        lab->SetVisible(false);
      }
    }
    return;
  }
  const CharacterStatsSnapshot snap = Session->GetCharacterStatsSnapshot();
  if (!snap.valid)
  {
    for (UGuiLabel *lab : labels)
    {
      if (lab)
      {
        lab->SetVisible(false);
      }
    }
    return;
  }
  auto setBar = [](UGuiLabel *lab, const char *name, float cur, float max)
  {
    if (!lab)
    {
      return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.0f/%.0f", name, cur, max);
    lab->SetText(buf);
    lab->SetVisible(true);
  };
  setBar(HealthLabel, "HP", snap.vitals.health, snap.vitals.maxHealth);
  setBar(SatietyLabel, "Food", snap.vitals.satiety, snap.vitals.maxSatiety);
  setBar(ThirstLabel, "Water", snap.vitals.thirst, snap.vitals.maxThirst);
  setBar(FatigueLabel, "Fatigue", snap.vitals.fatigue, snap.vitals.maxFatigue);
  setBar(BreathLabel, "Breath", snap.vitals.breath, snap.vitals.maxBreath);
}

} // namespace cutum
