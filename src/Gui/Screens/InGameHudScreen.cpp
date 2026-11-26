#include "Gui/Screens/InGameHudScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
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
#include <type_traits>
#include <typeinfo>

#if defined(__ANDROID__)
#include "Gui/Widgets/GuiTouchControls.h"
#endif

namespace cutum
{

namespace
{
// Grow the only potentially throwing destination before any widget becomes
// reachable through the HUD. Moving a unique_ptr into reserved space is noexcept.
template<class Widget, size_t N>
void AttachHudGroup(UGuiPanel &root, std::array<std::unique_ptr<Widget>, N> &widgets)
{
  root.ReserveChildren(root.GetChildren().size() + N);
  for (auto &widget : widgets)
    root.AddChild(std::move(widget));
}
} // namespace

struct UInGameHudScreen::SelectSlotAction
{
  UInGameHudScreen *owner;
  size_t bar, index;
  void operator()() const
  {
    if (!owner->Session->ApplyPendingAssignment(bar, index))
      owner->Session->SelectSlot(bar, index);
  }
};

struct UInGameHudScreen::DragSlotAction
{
  UInGameHudScreen *owner;
  size_t bar, index;
  void operator()() const
  {
    auto &session = *owner->Session;
    const auto entry = session.GetHotbarEntryRef(bar, index);
    if (!entry.empty)
    {
      SlotAddress address;
      address.surface = SlotSurface::Hotbar;
      address.bar = bar;
      address.slot = index;
      session.BeginDragFromSlot(address, entry);
    }
  }
};

UInGameHudScreen::UInGameHudScreen(UGameSession *session, const GuiTheme *theme,
                                   IUGuiIconSource *icons)
    : Session(session), Theme(theme), Icons(icons)
{
}

UInGameHudScreen::~UInGameHudScreen() = default;

InGameHudStorage UInGameHudScreen::InspectOwnedStorage(const UGuiContext &context,
                                                       const UGameSession *session,
                                                       const IUGuiIconSource *icons) const noexcept
{
  InGameHudStorage out;
  out.present = true;
#if defined(__ANDROID__)
  if (TouchControls)
    return out;
#endif
  if (typeid(*this) != typeid(UInGameHudScreen) || Context != &context ||
      context.GetScreen() == this || !Session || Session != session || Icons != icons ||
      Theme != &context.GetTheme() || !Renderer || Renderer != context.Renderer.get() ||
      !Root || Root.get() != RootPanel || typeid(*Root) != typeid(UGuiPanel) ||
      RootPanel->Theme != Theme)
    return out;
  if (PrimarySlots.size() != (HotbarBuilt ? 10 : 0) ||
      SecondarySlots.size() != (HotbarBuilt ? 30 : 0) ||
      (!HotbarBuilt && VitalsBuilt) || (!VitalsBuilt && ModeBadgeBuilt) ||
      (!ModeBadgeBuilt && DebugInfoBuilt))
    return out;

  const auto &children = Root->GetChildren();
  const size_t expected = (HotbarBuilt ? 42 : 0) + (VitalsBuilt ? 5 : 0) +
                          (ModeBadgeBuilt ? 2 : 0) + (DebugInfoBuilt ? 3 : 0);
  if (children.size() != expected)
    return out;
  StlSourceStorage objects, slots, indices, text, callbacks, total;
  out.widgets = 1;
  objects.Add(sizeof(UGuiPanel));
  slots.Buffer(children.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  size_t next = 0;
  const auto child_matches = [&](const auto *owner) {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(owner)>>;
    const auto &child = children[next++];
    if (!child || child.get() != owner || typeid(*child) != typeid(Widget) ||
        owner->Theme != Theme || !child->GetChildren().empty())
      return false;
    objects.Add(sizeof(Widget));
    slots.Buffer(child->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    ++out.widgets;
    if constexpr (std::is_same_v<Widget, UGuiLabel>)
      text.Text(owner->GetText());
    else if constexpr (std::is_same_v<Widget, UGuiSlot>)
    {
      text.Text(owner->Label);
      text.Text(owner->CornerHint);
      text.Text(owner->CountHint);
    }
    return true;
  };
  bool known_callbacks = true;
  if (HotbarBuilt)
  {
    for (size_t bar = 0; bar < 4; ++bar)
      for (size_t index = 0; index < 10; ++index)
      {
        const auto *slot = bar == 0 ? PrimarySlots[index] : SecondarySlots[(bar - 1) * 10 + index];
        if (!child_matches(slot))
          return out;
        ++out.slots;
        out.visible_slots += slot->IsVisible();
        const auto select = InspectKnownOwnerCallback<SelectSlotAction>(
            slot->OnClick, this, StlSourceStorage::SupportedProfile());
        const auto drag = InspectKnownOwnerCallback<DragSlotAction>(
            slot->OnBeginDrag, this, StlSourceStorage::SupportedProfile());
        const auto *select_action = slot->OnClick.target<SelectSlotAction>();
        const auto *drag_action = slot->OnBeginDrag.target<DragSlotAction>();
        known_callbacks &= select.supported && drag.supported && !slot->OnActivate &&
            select_action && select_action->bar == bar && select_action->index == index &&
            drag_action && drag_action->bar == bar && drag_action->index == index;
        out.callbacks += select.present + drag.present;
        callbacks.Add(select.requests);
        callbacks.Add(drag.requests);
      }
    if (!child_matches(Tooltip) || !child_matches(HotbarDivider))
      return out;
  }
  else if (Tooltip || HotbarDivider)
    return out;

  const auto labels_match = [&](bool built, const auto &labels) {
    for (const auto *label : labels)
      if (built ? !child_matches(label) : label != nullptr)
        return false;
    return true;
  };
  if (!labels_match(VitalsBuilt, std::array{HealthLabel, SatietyLabel, ThirstLabel, FatigueLabel, BreathLabel}) ||
      !labels_match(ModeBadgeBuilt, std::array{ModeBadge, CarryHint}) ||
      !labels_match(DebugInfoBuilt, std::array{WorldNameInfo, VersionInfo, CoordsInfo}))
    return out;
  out.bindings_match = true;
  out.groups = {HotbarBuilt, VitalsBuilt, ModeBadgeBuilt, DebugInfoBuilt};
  indices.Buffer(PrimarySlots.capacity(), sizeof(UGuiSlot *));
  indices.Buffer(SecondarySlots.capacity(), sizeof(UGuiSlot *));
  text.Text(GrabHintKey);
  text.Text(ThrowHintKey);
  out.screen_bytes = sizeof(*this);
  out.widget_bytes = objects.bytes.requested;
  out.children_bytes = slots.bytes.requested;
  out.slot_index_bytes = indices.bytes.requested;
  out.text_bytes = text.bytes.requested;
  out.callback_bytes = callbacks.bytes.requested;
  for (const auto bytes : {out.screen_bytes, out.widget_bytes, out.children_bytes,
                           out.slot_index_bytes, out.text_bytes, out.callback_bytes})
    total.Add(bytes);
  out.requested = total.bytes.requested;
  out.supported = known_callbacks && objects.bytes.supported && slots.bytes.supported &&
                  indices.bytes.supported && text.bytes.supported && callbacks.bytes.supported &&
                  total.bytes.supported;
  return out;
}

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
  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(false);
  Renderer = &ctx.GetRenderer();
  RootPanel = panel.get();
  Root = std::move(panel);
  PrimarySlots.clear();
  SecondarySlots.clear();
  HotbarDivider = nullptr;
  Tooltip = ModeBadge = CarryHint = nullptr;
  HealthLabel = SatietyLabel = ThirstLabel = FatigueLabel = BreathLabel = nullptr;
  WorldNameInfo = VersionInfo = CoordsInfo = nullptr;
  HotbarBuilt = VitalsBuilt = ModeBadgeBuilt = DebugInfoBuilt = false;
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

  std::array<std::unique_ptr<UGuiWidget>, 42> widgets;
  std::vector<UGuiSlot *> primary, secondary;
  primary.reserve(10);
  secondary.reserve(30);
  for (size_t i = 0; i < 40; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(Theme);
    const size_t bar = i / 10, index = i % 10;
    slot->SetOnClick(SelectSlotAction{this, bar, index});
    slot->SetOnBeginDrag(DragSlotAction{this, bar, index});
    if (bar == 0)
      slot->SetCornerHint(std::to_string((index + 1) % 10));
    (bar == 0 ? primary : secondary).push_back(slot.get());
    widgets[i] = std::move(slot);
  }

  auto tip = std::make_unique<UGuiLabel>(Theme, "");
  tip->SetTextAlign(GuiTextAlign::Center);
  tip->SetDrawBackground(true);
  tip->SetVisible(false);
  auto *tooltip = tip.get();
  widgets[40] = std::move(tip);

  auto divider = std::make_unique<UGuiPanel>(Theme);
  divider->SetDrawBackground(true);
  auto *hotbar_divider = divider.get();
  widgets[41] = std::move(divider);

  AttachHudGroup(*RootPanel, widgets);
  PrimarySlots = std::move(primary);
  SecondarySlots = std::move(secondary);
  Tooltip = tooltip;
  HotbarDivider = hotbar_divider;
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
  // Reserve a separate footer row for version/coordinates at every UI scale.
  const int footer = Theme->FontSizeBody + Theme->Padding * 3;
  const int rowY =
      ViewportH - std::max(Theme->HotbarMarginBottom, footer) - slotSize;

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

  const size_t bar_count = Session->GetBarCount();
  for (size_t i = 0; i < SecondarySlots.size(); ++i)
  {
    auto *slot = SecondarySlots[i];
    if (!slot)
      continue;
    const size_t bar = 1 + i / 10;
    const int column = int(i / 10), row = int(i % 10);
    slot->SetVisible(bar < bar_count);
    slot->SetBounds({ViewportW - Theme->HotbarSecondaryMarginRight - slotSize -
                         column * (slotSize + gap),
                     ViewportH -
                         std::max(Theme->HotbarSecondaryMarginBottom, footer) -
                         slotSize - row * (slotSize + gap),
                     slotSize, slotSize});
  }

  if (Tooltip)
  {
    Tooltip->SetVisible(false);
  }
}

UGuiSlot &UInGameHudScreen::HotbarSlot(size_t bar, size_t index) const
{
  return *(bar == 0 ? PrimarySlots[index] : SecondarySlots[(bar - 1) * 10 + index]);
}

void UInGameHudScreen::UpdateSlotData()
{
  for (size_t bar = 0; bar < 4; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    for (size_t i = 0; i < entries.size(); ++i)
    {
      auto &slot = HotbarSlot(bar, i);
      slot.SetLabel(entries[i].label);
      slot.SetCountHint(entries[i].CountLabel);
      slot.SetSelected(entries[i].selected);
    }
  }
}

void UInGameHudScreen::SyncSlotIcons()
{
  if (!HotbarBuilt || !Session || !Icons)
    return;
  for (size_t bar = 0; bar < 4; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    for (size_t i = 0; i < entries.size(); ++i)
    {
      const auto &entry = entries[i];
      const auto texture = entry.Entry.empty ? Icons->GetEntryIcon(entry.entryKind, entry.Id)
                                            : Icons->GetEntryIcon(entry.Entry);
      auto &slot = HotbarSlot(bar, i);
      const bool item = entry.entryKind == InventoryEntryKind::Item;
      slot.SetIconTexture(texture);
      slot.SetWearProgress(item ? entry.wear : 0.f);
      slot.SetBroken(item && entry.broken);
    }
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
      for (size_t bar = 0; bar < 4; ++bar)
        for (size_t index = 0; index < 10; ++index)
        {
          if (hit != &HotbarSlot(bar, index))
            continue;
          const auto entries = Session->GetBarSlots(bar);
          if (!entries[index].label.empty())
          {
            positionTip(FormatHotbarTooltip(entries[index]), PointerX, PointerY);
            return;
          }
          break;
        }
    }
  }

  for (size_t bar = 0; bar < 2; ++bar)
  {
    const auto entries = Session->GetBarSlots(bar);
    const size_t index = Session->GetSelectedSlot(bar);
    if (index < entries.size() && !entries[index].label.empty())
    {
      const auto &bounds = HotbarSlot(bar, index).GetBounds();
      positionTip(FormatHotbarTooltip(entries[index]), bounds.X + bounds.W / 2,
                  bounds.Y + bounds.H / 2);
      return;
    }
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
  auto hint = std::make_unique<UGuiLabel>(Theme, "");
  hint->SetTextAlign(GuiTextAlign::Center);
  hint->SetDrawBackground(true);
  auto *mode_badge = badge.get();
  auto *carry_hint = hint.get();
  std::array<std::unique_ptr<UGuiLabel>, 2> labels{std::move(badge), std::move(hint)};
  AttachHudGroup(*RootPanel, labels);
  ModeBadge = mode_badge;
  CarryHint = carry_hint;
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
                              std::max(Theme->HotbarMarginBottom,
                                       Theme->FontSizeBody + pad * 3) -
                              line * 2,
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
  auto makeBadge = [this](bool alignRight)
  {
    auto lab = std::make_unique<UGuiLabel>(Theme, "");
    lab->SetInfoBadgeStyle(true, alignRight);
    return lab;
  };
  std::array<std::unique_ptr<UGuiLabel>, 3> labels{makeBadge(false), makeBadge(false), makeBadge(true)};
  labels[1]->SetText(kCubatariumVersion);
  const std::array<UGuiLabel *, 3> aliases{labels[0].get(), labels[1].get(), labels[2].get()};
  AttachHudGroup(*RootPanel, labels);
  WorldNameInfo = aliases[0];
  VersionInfo = aliases[1];
  CoordsInfo = aliases[2];
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
  const int coordinateWidth = std::min(w, std::max(1, ViewportW / 3 - pad));
  CoordsInfo->SetBounds({right - coordinateWidth, oy + ViewportH - pad - line,
                         coordinateWidth, line});
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
  std::array<std::unique_ptr<UGuiLabel>, 5> labels;
  for (auto &label : labels)
  {
    label = std::make_unique<UGuiLabel>(Theme, "");
    label->SetDrawBackground(true);
    label->SetVisible(false);
  }
  const std::array<UGuiLabel *, 5> aliases{
      labels[0].get(), labels[1].get(), labels[2].get(), labels[3].get(), labels[4].get()};
  AttachHudGroup(*RootPanel, labels);
  HealthLabel = aliases[0];
  SatietyLabel = aliases[1];
  ThirstLabel = aliases[2];
  FatigueLabel = aliases[3];
  BreathLabel = aliases[4];
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
