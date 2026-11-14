#include "Gui/Screens/CharacterSheetScreen.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiKeyCodes.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Interfaces/IUHotbarViewModel.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Layout/PlayerWindowLayout.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Items/ItemDefinitionStorage.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Persistence/WorldPersistence.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

constexpr int kSheetW = 560;
constexpr int kSheetH = 500;
constexpr int kLineH = 20;
constexpr int kSlotSize = 48;
constexpr int kSlotGap = 6;

bool PointInBounds(int x, int y, const GuiRect &b)
{
  return x >= b.X && y >= b.Y && x < b.X + b.W && y < b.Y + b.H;
}

} // namespace

UCharacterSheetScreen::UCharacterSheetScreen(
    IUCharacterStatsViewModel *stats, UCreaturePreviewRenderer *creaturePreview,
    IUGuiIconSource *icons)
    : Stats(stats), CreaturePreview(creaturePreview), Icons(icons)
{
}

UCharacterSheetScreen::~UCharacterSheetScreen()
{
  if (PreviewTexture != 0)
  {
    glDeleteTextures(1, &PreviewTexture);
    PreviewTexture = 0;
  }
}

std::string UCharacterSheetScreen::FormatBar(const char *name, float cur,
                                             float max)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s: %.0f / %.0f", name, cur, max);
  return buf;
}

void UCharacterSheetScreen::SetVisible(bool visible)
{
  if (!visible && Search)
    Search->SetFocused(false);
  if (!visible)
    if (auto *session = dynamic_cast<UGameSession *>(Stats))
      session->CancelDrag();
  if (visible != Visible)
  {
    EditingAppearance = Section == 3;
    if (!DraftInitialized)
      ResetAppearanceDraft();
  }
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (!Visible)
  {
    // UX U4: keep the preview texture across opens - it was deleted and
    // re-rendered (full creature visual construction) on every open.
  }
  else if (Visible && Built)
  {
    RefreshLabels();
  }
}

GuiRect UCharacterSheetScreen::GetMainSlotBounds() const
{
  return ToolSlots.empty() ? GuiRect{} : ToolSlots[0]->GetBounds();
}
GuiRect UCharacterSheetScreen::GetDropButtonBounds() const
{
  return DropButton ? DropButton->GetBounds() : GuiRect{};
}
GuiRect UCharacterSheetScreen::GetDeleteButtonBounds() const
{
  return DeleteButton ? DeleteButton->GetBounds() : GuiRect{};
}
void UCharacterSheetScreen::Toggle() { SetVisible(!Visible); }

bool UCharacterSheetScreen::IsSearchFocused() const
{
  return Search && Search->IsFocused();
}
bool UCharacterSheetScreen::RouteTextKey(const GuiKeyEvent &e)
{
  return IsSearchFocused() ? Search->OnKey(e) : false;
}
bool UCharacterSheetScreen::RouteTextChar(const GuiCharEvent &e)
{
  return IsSearchFocused() ? Search->OnChar(e) : false;
}
void UCharacterSheetScreen::BuildEquipmentPanel(UGuiPanel &body,
                                                const GuiTheme &theme)
{
  // Paper-doll panel (preview + equipment).
  {
    auto doll = std::make_unique<UGuiPanel>(&theme);
    DollPanel = doll.get();
    DollPanel->SetDrawBackground(true);

    auto eqHead = std::make_unique<UGuiLabel>(&theme, "Equipment");
    EquipmentHeader = eqHead.get();
    DollPanel->AddChild(std::move(eqHead));

    auto preview = std::make_unique<UGuiPreviewViewport>(&theme);
    PreviewViewport = preview.get();
    PreviewViewport->SetOnRotationChanged([this](float /*yaw*/, float /*pitch*/)
                                          { RenderCharacterPreview(); });
    DollPanel->AddChild(std::move(preview));

    ArmorSlots.clear();
    ToolSlots.clear();
    const char *armorNames[4] = {"Head", "Chest", "Legs", "Feet"};
    for (int i = 0; i < 4; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetLabel(std::string(armorNames[i]).substr(0, 1));
      slot->SetIconTexture(0);
      const size_t armorIndex = static_cast<size_t>(i);
      SlotAddress address;
      address.surface = SlotSurface::CharacterArmor;
      address.slot = armorIndex;
      slot->SetOnBeginDrag(
          [this, address, armorIndex]()
          {
            auto *session = dynamic_cast<UGameSession *>(Stats);
            if (!session)
            {
              return;
            }
            const InventoryEntryRef entry =
                session->GetArmorEntryRef(armorIndex);
            if (!entry.empty)
            {
              session->BeginDragFromSlot(address, entry);
            }
          });
      slot->SetOnClick(
          [this, address, armorIndex]()
          {
            auto *session = dynamic_cast<UGameSession *>(Stats);
            if (!session)
            {
              return;
            }
            const InventoryEntryRef entry =
                session->GetArmorEntryRef(armorIndex);
            if (entry.empty)
            {
              return;
            }
            session->BeginDragFromSlot(address, entry);
          });
      slot->SetOnActivate(
          [this, address, armorIndex](const GuiMouseEvent &event)
          {
            auto *session = dynamic_cast<UGameSession *>(Stats);
            if (!session)
              return;
            const auto entry = session->GetArmorEntryRef(armorIndex);
            if (entry.empty)
              return;
            session->BeginDragFromSlot(address, entry);
            if (event.Mods & GuiKey::ModShift)
            {
              SlotAddress bag;
              bag.surface = SlotSurface::BackpackReturn;
              session->DropOnSlot(bag);
            }
            RefreshLabels();
          });
      ArmorSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }
    for (int i = 0; i < 2; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetLabel(i == 0 ? "RH" : "LH");
      slot->SetIconTexture(0);
      if (i == 0)
      {
        slot->SetOnBeginDrag(
            [this]()
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
              {
                return;
              }
              SlotAddress address;
              address.surface = SlotSurface::Hotbar;
              address.bar = session->GetCharacterStatsSnapshot().activeRow;
              address.slot = session->GetSelectedSlot(address.bar);
              const InventoryEntryRef entry =
                  session->GetHotbarEntryRef(address.bar, address.slot);
              if (!entry.empty)
              {
                session->BeginDragFromSlot(address, entry);
              }
            });
        slot->SetOnClick(
            [this]()
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
              {
                return;
              }
              SlotAddress address;
              address.surface = SlotSurface::Hotbar;
              address.bar = session->GetCharacterStatsSnapshot().activeRow;
              address.slot = session->GetSelectedSlot(address.bar);
              const InventoryEntryRef entry =
                  session->GetHotbarEntryRef(address.bar, address.slot);
              if (!entry.empty)
              {
                session->InventoryClick(address.bar * 10 + address.slot, false,
                                        false, false);
                return;
              }
              session->ApplyPendingAssignment(address.bar, address.slot);
            });
        slot->SetOnActivate(
            [this](const GuiMouseEvent &event)
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
                return;
              const auto snapshot = session->GetCharacterStatsSnapshot();
              const auto index = snapshot.activeRow * 10 +
                                 session->GetSelectedSlot(snapshot.activeRow);
              session->InventoryClick(index,
                                      event.Button == GuiMouseButton::Right,
                                      (event.Mods & GuiKey::ModShift) != 0,
                                      event.ClickCount == 2 &&
                                          event.Button == GuiMouseButton::Left);
              RefreshLabels();
            });
      }
      else
      {
        SlotAddress address;
        address.surface = SlotSurface::CharacterOffhand;
        slot->SetOnBeginDrag(
            [this, address]()
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
              {
                return;
              }
              const InventoryEntryRef entry = session->GetOffhandEntryRef();
              if (!entry.empty)
              {
                session->BeginDragFromSlot(address, entry);
              }
            });
        slot->SetOnClick(
            [this, address]()
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
              {
                return;
              }
              const InventoryEntryRef entry = session->GetOffhandEntryRef();
              if (entry.empty)
              {
                return;
              }
              session->BeginDragFromSlot(address, entry);
            });
      }
      if (i == 1)
        slot->SetOnActivate(
            [this](const GuiMouseEvent &event)
            {
              auto *session = dynamic_cast<UGameSession *>(Stats);
              if (!session)
                return;
              const auto entry = session->GetOffhandEntryRef();
              if (entry.empty)
                return;
              SlotAddress source;
              source.surface = SlotSurface::CharacterOffhand;
              session->BeginDragFromSlot(source, entry);
              if (event.Mods & GuiKey::ModShift)
              {
                SlotAddress bag;
                bag.surface = SlotSurface::BackpackReturn;
                session->DropOnSlot(bag);
              }
              RefreshLabels();
            });
      ToolSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }

    body.AddChild(std::move(doll));
  }
}

void UCharacterSheetScreen::Build(UGuiContext &ctx)
{
  const GuiTheme &theme = ctx.GetTheme();
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0)
  {
    ViewportW = w;
  }
  if (h > 0)
  {
    ViewportH = h;
  }

  auto root = std::make_unique<UGuiPanel>(&theme);
  root->SetDrawBackground(false);
  root->SetBounds({0, 0, ViewportW, ViewportH});

  auto window = std::make_unique<UGuiWindow>(&theme, "Player");
  Window = window.get();

  auto body = std::make_unique<UGuiPanel>(&theme);
  BodyPanel = body.get();
  body->SetDrawBackground(false);

  auto meta = std::make_unique<UGuiLabel>(&theme, "");
  TitleMeta = meta.get();
  body->AddChild(std::move(meta));

  auto mode = std::make_unique<UGuiLabel>(&theme, "");
  ModeLabel = mode.get();
  ModeLabel->SetUseSecondaryColor(true);
  body->AddChild(std::move(mode));

  auto tabs = std::make_unique<UGuiTabBar>(&theme);
  Sections = tabs.get();
  Sections->SetTabs(
      {"Backpack", "Equipment", "Status", "Appearance", "Catalog"});
  Sections->SetActiveTab(Section);
  Sections->SetOnTabChanged(
      [this](int section)
      {
        if (section == 4)
        {
          if (auto *session = dynamic_cast<UGameSession *>(Stats))
            session->OpenPlayerWindow(4);
        }
        else
          SetSection(section);
      });
  body->AddChild(std::move(tabs));
  auto search = std::make_unique<UGuiTextInput>(&theme);
  Search = search.get();
  Search->SetClipboard(ctx.GetClipboard());
  Search->SetOnEdited([this] { RefreshLabels(); });
  body->AddChild(std::move(search));
  auto sort = std::make_unique<UGuiButton>(&theme, "Sort");
  SortButton = sort.get();
  SortButton->SetOnClick(
      [this]
      {
        if (RecoveryMode)
        {
          const auto size = Stats->GetCharacterStatsSnapshot().recovery.size();
          RecoveryOffset = RecoveryOffset + 40 < size ? RecoveryOffset + 40 : 0;
        }
        else if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->SortInventory();
        RefreshLabels();
      });
  body->AddChild(std::move(sort));
  auto drop = std::make_unique<UGuiButton>(&theme, "Drop");
  DropButton = drop.get();
  DropButton->SetOnClick(
      [this]
      {
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->DiscardInventory();
        RefreshLabels();
      });
  body->AddChild(std::move(drop));
  auto remove = std::make_unique<UGuiButton>(&theme, "Clear template");
  DeleteButton = remove.get();
  remove->SetOnClick(
      [this]
      {
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->DeleteCreativeAssignment();
        RefreshLabels();
      });
  body->AddChild(std::move(remove));
  auto recovery = std::make_unique<UGuiButton>(&theme, "Recover");
  RecoveryButton = recovery.get();
  RecoveryButton->SetOnClick(
      [this]
      {
        RecoveryMode = !RecoveryMode;
        RecoveryOffset = 0;
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->CancelDrag();
        RefreshLabels();
      });
  body->AddChild(std::move(recovery));
  auto hint = std::make_unique<UGuiLabel>(
      &theme, "Click: move | Right: split/place 1 | Shift: quick | Ctrl: pin");
  InventoryHint = hint.get();
  body->AddChild(std::move(hint));
  auto toggle = std::make_unique<UGuiButton>(&theme, "Appearance");
  AppearanceToggle = toggle.get();
  toggle->SetOnClick([this] { SetSection(EditingAppearance ? 1 : 3); });
  body->AddChild(std::move(toggle));
  auto customization = std::make_unique<UGuiPanel>(&theme);
  AppearancePanel = customization.get();
  for (int row = 0; row < 6; ++row)
  {
    auto button = std::make_unique<UGuiButton>(&theme, "");
    AppearanceButtons.push_back(button.get());
    button->SetOnClick(
        [this, row]
        {
          auto cycle = [](auto options, std::string &value)
          {
            size_t i = 0;
            while (i < options.size() && value != options[i])
              ++i;
            value = options[(i + 1) % options.size()];
          };
          if (row == 0)
            cycle(HumanAppearance::Bodies, DraftAppearance.bodyVariant);
          if (row == 1)
          {
            const std::array<const char *, 3> outfits{"", "human_adventurer",
                                                      "human_guard"};
            cycle(outfits, DraftSkin);
          }
          if (row == 2)
            cycle(HumanAppearance::Hairstyles, DraftAppearance.hairstyleId);
          if (row == 3)
            cycle(HumanAppearance::Colors, DraftAppearance.hairColorId);
          if (row == 4 && Stats &&
              !Stats->ApplyHumanAppearance(DraftAppearance, DraftSkin))
            return;
          if (row >= 4)
          {
            SetSection(1);
            ResetAppearanceDraft();
            Relayout();
          }
          RefreshAppearanceControls();
          RefreshLabels();
        });
    customization->AddChild(std::move(button));
  }
  body->AddChild(std::move(customization));

  BuildEquipmentPanel(*body, theme);

  {
    auto bag = std::make_unique<UGuiPanel>(&theme);
    BagPanel = bag.get();
    BagPanel->SetDrawBackground(true);
    BagSlots.clear();
    for (size_t i = 0; i < 40; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetOnBeginDrag(
          [this, i]
          {
            if (RecoveryMode)
              return;
            if (auto *session = dynamic_cast<UGameSession *>(Stats))
              session->InventoryClick(i, false, false, false);
          });
      slot->SetOnActivate(
          [this, i](const GuiMouseEvent &event)
          {
            auto *session = dynamic_cast<UGameSession *>(Stats);
            if (!session)
              return;
            if (RecoveryMode)
            {
              session->RecoverInventory(RecoveryOffset + i);
              RefreshLabels();
              return;
            }
            if (event.Mods & GuiKey::ModControl)
            {
              if (auto *actor = session->GetWorld()->GetControlledCreature())
                if (auto *entry = actor->GetInventory().GetSlotMutable(i))
                  entry->pinned = !entry->pinned;
            }
            else
              session->InventoryClick(i, event.Button == GuiMouseButton::Right,
                                      (event.Mods & GuiKey::ModShift) != 0,
                                      event.ClickCount == 2 &&
                                          event.Button == GuiMouseButton::Left);
            RefreshLabels();
          });
      BagSlots.push_back(slot.get());
      BagPanel->AddChild(std::move(slot));
    }
    body->AddChild(std::move(bag));
  }

  // Stats panel (vitals + attributes).
  {
    auto scroll = std::make_unique<UGuiScrollView>(&theme);
    StatsScroll = scroll.get();
    auto *stats = &scroll->Content();
    StatsPanel = stats;
    StatsPanel->SetDrawBackground(true);

    auto vitHead = std::make_unique<UGuiLabel>(&theme, "Vitals");
    VitalsHeader = vitHead.get();
    StatsPanel->AddChild(std::move(vitHead));

    VitalLabels.clear();
    for (int i = 0; i < 7; ++i)
    {
      auto lab = std::make_unique<UGuiLabel>(&theme, "");
      VitalLabels.push_back(lab.get());
      StatsPanel->AddChild(std::move(lab));
    }

    auto attrHead = std::make_unique<UGuiLabel>(&theme, "Attributes");
    AttrsHeader = attrHead.get();
    StatsPanel->AddChild(std::move(attrHead));

    AttrLabels.clear();
    for (int i = 0; i < 7; ++i)
    {
      auto lab = std::make_unique<UGuiLabel>(&theme, "");
      AttrLabels.push_back(lab.get());
      StatsPanel->AddChild(std::move(lab));
    }

    body->AddChild(std::move(scroll));
  }

  window->AddChild(std::move(body));
  root->AddChild(std::move(window));
  auto tooltip = std::make_unique<UGuiLabel>(&theme, "");
  Tooltip = tooltip.get();
  tooltip->SetVisible(false);
  tooltip->SetZOrder(1000);
  root->AddChild(std::move(tooltip));
  Root = std::move(root);
  Built = true;
  Relayout();
  RefreshLabels();
  SetVisible(false);
}

void UCharacterSheetScreen::SetSection(int section)
{
  Section = std::clamp(section, 0, 3);
  EditingAppearance = Section == 3;
  if (Sections && Sections->GetActiveTab() != Section)
    Sections->SetActiveTab(Section);
  if (Search)
    Search->SetFocused(false);
  if (auto *session = dynamic_cast<UGameSession *>(Stats))
    session->CancelDrag();
  if (!DraftInitialized)
    ResetAppearanceDraft();
  Relayout();
  RefreshLabels();
}
bool UCharacterSheetScreen::IsEditingText() const
{
  return Search && Search->IsFocused();
}
bool UCharacterSheetScreen::PickBackpackSlot(int x, int y, size_t &slot) const
{
  if (!Visible || Section > 1 || RecoveryMode)
    return false;
  for (size_t i = 0; i < BagSlots.size(); ++i)
    if (BagSlots[i]->IsVisible() && BagSlots[i]->GetBounds().Contains(x, y))
    {
      slot = i;
      return true;
    }
  return false;
}
void UCharacterSheetScreen::Relayout()
{
  if (!Root || !Window)
    return;
  Root->SetBounds({0, 0, ViewportW, ViewportH});
  const int pad = Scaled(8), line = Scaled(24), tabH = Scaled(32);
  const int w = std::max(300, std::min(ViewportW - pad * 2, Scaled(1080)));
  const int h = std::max(300, std::min(ViewportH - pad * 2, Scaled(660)));
  Window->SetBounds({(ViewportW - w) / 2, (ViewportH - h) / 2, w, h});
  auto c = Window->GetClientArea();
  BodyPanel->SetBounds(c);
  WideLayout = c.W >= Scaled(740);
  TitleMeta->SetBounds({c.X + pad, c.Y + pad, c.W - pad * 2, line});
  AppearanceToggle->SetVisible(true);
  AppearanceToggle->SetBounds(
      {c.X + c.W - Scaled(135), c.Y + pad, Scaled(125), line});
  TitleMeta->SetBounds({c.X + pad, c.Y + pad, c.W - Scaled(155), line});
  ModeLabel->SetBounds({c.X + pad, c.Y + pad + line, c.W - pad * 2, line});
  Sections->SetBounds({c.X + pad, c.Y + pad + line * 2, c.W - pad * 2, tabH});
  const auto shared = PlayerWindowLayout::Compute(
      ViewportW, ViewportH, Metrics, GetContentOffsetX(), GetContentOffsetY());
  if (Hosted)
  {
    Window->SetChromeVisible(false);
    Window->SetBounds(shared.Body);
    BodyPanel->SetBounds(shared.Body);
    TitleMeta->SetVisible(false);
    ModeLabel->SetVisible(false);
    Sections->SetVisible(false);
    AppearanceToggle->SetVisible(false);
  }
  const int top = Hosted ? shared.Body.Y : c.Y + pad + line * 2 + tabH + pad;
  const int contentH =
      Hosted ? shared.Body.H : std::max(100, c.Y + c.H - top - pad);
  const int dollW = Hosted ? shared.Preview.W
                           : ((WideLayout || Section == 1 || Section == 3)
                                  ? std::min(Scaled(320), c.W / 3)
                                  : 0);
  const int mainX = Hosted ? shared.Content.X
                           : c.X + pad + (dollW ? dollW + pad : 0),
            mainW = Hosted ? shared.Content.W
                           : c.W - pad * 2 - (dollW ? dollW + pad : 0);
  DollPanel->SetVisible(dollW > 0 || Section == 3);
  DollPanel->SetBounds(
      Hosted ? shared.Preview
             : GuiRect{c.X + pad, top, dollW ? dollW : c.W / 3, contentH});
  const bool bagVisible = Section < 2;
  BagPanel->SetVisible(bagVisible);
  Search->SetVisible(bagVisible);
  SortButton->SetVisible(bagVisible);
  RecoveryButton->SetVisible(bagVisible);
  DropButton->SetVisible(bagVisible);
  InventoryHint->SetVisible(bagVisible && mainW >= Scaled(650));
  Search->SetBounds(
      {mainX, top, std::max(40, mainW - Scaled(190)), line + pad});
  SortButton->SetBounds(
      {mainX + mainW - Scaled(182), top, Scaled(65), line + pad});
  RecoveryButton->SetBounds(
      {mainX + mainW - Scaled(110), top, Scaled(110), line + pad});
  DropButton->SetBounds({mainX + mainW - Scaled(75),
                         top + contentH - line - pad, Scaled(75), line + pad});
  auto dropBounds = DropButton->GetBounds();
  DeleteButton->SetBounds(
      {dropBounds.X - Scaled(150), dropBounds.Y, Scaled(145), dropBounds.H});
  DeleteButton->SetVisible(bagVisible && Stats &&
                           Stats->GetCharacterStatsSnapshot().gameMode ==
                               WorldGameMode::Creative);
  const int gridTop = top + line + pad * 2;
  const int slot =
      std::max(20, std::min((mainW - pad) / 10 - pad / 2,
                            (contentH - (line + pad) * 2 - pad * 4) / 4 - pad));
  BagPanel->SetBounds({mainX, gridTop, mainW, 4 * (slot + pad)});
  for (size_t i = 0; i < 40; ++i)
    BagSlots[i]->SetBounds({mainX + pad / 2 + int(i % 10) * (slot + pad / 2),
                            gridTop + pad / 2 + int(i / 10) * (slot + pad),
                            slot, slot});
  InventoryHint->SetBounds({mainX, top + contentH - line - pad,
                            std::max(40, mainW - Scaled(235)), line * 2});
  StatsScroll->SetVisible(Section == 2);
  StatsScroll->SetBounds({mainX, top, mainW, contentH});
  auto labelHeight = [&](UGuiLabel *label)
  {
    auto b = label->GetBounds();
    b.H = line;
    label->SetBounds(b);
  };
  labelHeight(VitalsHeader);
  labelHeight(AttrsHeader);
  for (auto *label : VitalLabels)
    labelHeight(label);
  for (auto *label : AttrLabels)
    labelHeight(label);
  for (auto *label : EffectLabels)
    labelHeight(label);
  StatsScroll->LayoutContent(pad / 2, pad);
  AppearancePanel->SetBounds({mainX, top, mainW, contentH});
  AppearancePanel->SetVisible(EditingAppearance);
  for (size_t i = 0; i < AppearanceButtons.size(); ++i)
    AppearanceButtons[i]->SetBounds({mainX + pad,
                                     top + pad + int(i) * (line + pad),
                                     mainW - pad * 2, line + pad / 2});
  const int left = Hosted ? shared.Preview.X : c.X + pad,
            dw = dollW ? dollW : c.W / 3;
  const int eqSlot = std::max(
      24, std::min({Scaled(42), (contentH - line - pad * 5) / 6,
                    (dw - pad * 4) / 4, (contentH - line - pad * 5 - 48) / 3}));
  const int preview =
      std::max(48, std::min({dw - eqSlot * 2 - pad * 4, Scaled(180),
                             contentH - line - eqSlot * 3 - pad * 5}));
  const int px = left + (dw - preview) / 2, py = top + line + eqSlot + pad * 2;
  PreviewViewport->SetBounds({px, py, preview, preview});
  if (EditingAppearance)
    PreviewViewport->SetBounds({left + pad, top + line, dw - pad * 2,
                                std::min(contentH - line, dw * 2)});
  EquipmentHeader->SetBounds({left + pad, top + pad, dw - pad * 2, line});
  EquipmentHeader->SetVisible(!EditingAppearance);
  const std::array<GuiRect, 4> armor{
      {{px + (preview - eqSlot) / 2, py - eqSlot - pad, eqSlot, eqSlot},
       {px - eqSlot - pad, py + (preview - eqSlot) / 2, eqSlot, eqSlot},
       {px + preview + pad, py + (preview - eqSlot) / 2, eqSlot, eqSlot},
       {px + (preview - eqSlot) / 2, py + preview + pad, eqSlot, eqSlot}}};
  for (size_t i = 0; i < 4; ++i)
  {
    ArmorSlots[i]->SetBounds(armor[i]);
    ArmorSlots[i]->SetVisible(!EditingAppearance);
  }
  for (size_t i = 0; i < 2; ++i)
  {
    ToolSlots[i]->SetBounds({left + pad + int(i) * (dw / 2),
                             py + preview + eqSlot + pad * 3, eqSlot, eqSlot});
    ToolSlots[i]->SetVisible(!EditingAppearance);
  }
}

bool UCharacterSheetScreen::PickArmorSlot(int x, int y, size_t &outSlot) const
{
  if (EditingAppearance || !DollPanel->IsVisible())
    return false;
  for (size_t i = 0; i < ArmorSlots.size(); ++i)
  {
    if (ArmorSlots[i] && PointInBounds(x, y, ArmorSlots[i]->GetBounds()))
    {
      outSlot = i;
      return true;
    }
  }
  return false;
}

bool UCharacterSheetScreen::PickMainSlot(int x, int y) const
{
  if (EditingAppearance || !DollPanel->IsVisible())
    return false;
  if (ToolSlots.empty() || !ToolSlots[0])
  {
    return false;
  }
  return PointInBounds(x, y, ToolSlots[0]->GetBounds());
}

bool UCharacterSheetScreen::PickOffhandSlot(int x, int y) const
{
  if (EditingAppearance || !DollPanel->IsVisible())
    return false;
  if (ToolSlots.size() < 2 || !ToolSlots[1])
  {
    return false;
  }
  return PointInBounds(x, y, ToolSlots[1]->GetBounds());
}

bool UCharacterSheetScreen::PickDoll(int x, int y) const
{
  return PreviewViewport && PointInBounds(x, y, PreviewViewport->GetBounds());
}

void UCharacterSheetScreen::RefreshLabels()
{
  if (!Stats || !Built)
  {
    return;
  }
  const CharacterStatsSnapshot snap = Stats->GetCharacterStatsSnapshot();
  if (!DraftInitialized || DraftOwnerId != snap.OwnerId ||
      DraftBase != snap.humanAppearance.Key() + "|" + snap.skinId)
    ResetAppearanceDraft();
  if (TitleMeta)
  {
    std::ostringstream oss;
    oss << (snap.valid ? snap.displayName : "(none)");
    if (snap.valid && !snap.skinId.empty())
    {
      oss << " [" << snap.skinId << "]";
    }
    if (snap.valid)
    {
      oss << " (" << snap.typeId << ")";
    }
    TitleMeta->SetText(oss.str());
  }
  Sections->SetTabEnabled(
      {true, true, true, true, snap.gameMode == WorldGameMode::Creative});
  if (ModeLabel)
  {
    if (snap.gameMode == WorldGameMode::Creative)
    {
      ModeLabel->SetText(
          "Creative: instant mining + loot; no wear; vitals frozen");
    }
    else
    {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "Mode: Survival / %s",
                    WorldDifficultyToString(snap.difficulty));
      ModeLabel->SetText(buf);
    }
  }
  if (VitalLabels.size() >= 7 && snap.valid)
  {
    const auto &v = snap.vitals;
    VitalLabels[0]->SetText(FormatBar("Health", v.health, v.maxHealth));
    VitalLabels[1]->SetText(FormatBar("Satiety", v.satiety, v.maxSatiety));
    VitalLabels[2]->SetText(FormatBar("Thirst", v.thirst, v.maxThirst));
    VitalLabels[3]->SetText(FormatBar("Fatigue", v.fatigue, v.maxFatigue));
    VitalLabels[4]->SetText(FormatBar("Breath", v.breath, v.maxBreath));
    char wounds[64];
    std::snprintf(wounds, sizeof(wounds), "Fatal wounds: %d / %d",
                  v.fatalWounds, v.maxFatalWounds);
    VitalLabels[5]->SetText(wounds);
    char armor[48];
    std::snprintf(armor, sizeof(armor), "Armor: %.0f", v.armor);
    VitalLabels[6]->SetText(armor);
  }
  if (AttrLabels.size() >= 7 && snap.valid)
  {
    const auto &a = snap.attributes;
    auto setAttr = [](UGuiLabel *lab, const char *name, int value)
    {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%s: %d", name, value);
      lab->SetText(buf);
    };
    setAttr(AttrLabels[0], "Strength", a.strength);
    setAttr(AttrLabels[1], "Agility", a.agility);
    setAttr(AttrLabels[2], "Endurance", a.endurance);
    setAttr(AttrLabels[3], "Accuracy", a.accuracy);
    setAttr(AttrLabels[4], "Intelligence", a.intelligence);
    setAttr(AttrLabels[5], "Luck", a.luck);
    setAttr(AttrLabels[6], "Perception", a.perception);
  }

  if (Icons)
  {
    for (int i = 0; i < 4 && i < static_cast<int>(ArmorSlots.size()); ++i)
    {
      const auto &s = snap.equippedArmor[i];
      if (ArmorSlots[i] && !s.itemId.empty())
      {
        ArmorSlots[i]->SetIconTexture(Icons->GetItemIconTexture(s.itemId));
        ArmorSlots[i]->SetWearProgress(s.wear);
        ArmorSlots[i]->SetBroken(s.broken);
        ArmorSlots[i]->SetDimmed(false);
      }
      else if (ArmorSlots[i])
      {
        ArmorSlots[i]->SetIconTexture(0);
        ArmorSlots[i]->SetWearProgress(0.f);
        ArmorSlots[i]->SetBroken(false);
        ArmorSlots[i]->SetDimmed(false);
      }
    }

    for (int i = 0; i < 2 && i < static_cast<int>(ToolSlots.size()); ++i)
    {
      const auto &s = snap.equippedTools[i];
      if (ToolSlots[i])
      {
        const auto &active = snap.slots[snap.activeRow * 10 + snap.activeSlot];
        ToolSlots[i]->SetCountHint(i == 0 && !active.empty &&
                                           !active.IsCatalogReference()
                                       ? std::to_string(active.count)
                                       : "");
      }
      if (ToolSlots[i] && !s.itemId.empty())
      {
        const GLuint tex = s.isBlock ? Icons->GetBlockIconTexture(s.itemId)
                                     : Icons->GetItemIconTexture(s.itemId);
        ToolSlots[i]->SetIconTexture(tex);
        ToolSlots[i]->SetWearProgress(s.wear);
        ToolSlots[i]->SetBroken(s.broken);
        ToolSlots[i]->SetDimmed(false);
      }
      else if (ToolSlots[i])
      {
        ToolSlots[i]->SetIconTexture(0);
        ToolSlots[i]->SetWearProgress(0.f);
        ToolSlots[i]->SetBroken(false);
        ToolSlots[i]->SetDimmed(false);
      }
    }
    std::string query = Search ? Search->GetText() : "";
    std::replace(query.begin(), query.end(), '_', ' ');
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (size_t i = 0; i < BagSlots.size(); ++i)
    {
      const InventoryEntryRef empty;
      const auto &entry = RecoveryMode
                              ? (RecoveryOffset + i < snap.recovery.size()
                                     ? snap.recovery[RecoveryOffset + i]
                                     : empty)
                              : snap.slots[i];
      auto *slot = BagSlots[i];
      const auto tex = entry.empty ? 0
                       : entry.kind == InventoryEntryKind::Block
                           ? Icons->GetBlockIconTexture(entry.Id)
                           : Icons->GetItemIconTexture(entry.Id);
      slot->SetLabel(RecoveryMode ? entry.Id : "");
      slot->SetIconTexture(tex);
      slot->SetWearProgress(entry.wear);
      slot->SetBroken(entry.broken);
      slot->SetCornerHint(!entry.empty && entry.pinned ? "*" : "");
      slot->SetCountHint(entry.empty || entry.IsCatalogReference()
                             ? ""
                             : std::to_string(entry.count));
      std::string text = entry.Id;
      std::replace(text.begin(), text.end(), '_', ' ');
      std::transform(text.begin(), text.end(), text.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      slot->SetDimmed(!query.empty() && text.find(query) == std::string::npos);
      slot->SetSelected(!RecoveryMode && i / 10 == snap.activeRow &&
                        i % 10 == snap.activeSlot);
      slot->SetVisible(true);
    }
  }
  if (RecoveryButton)
  {
    RecoveryButton->SetLabel(
        RecoveryMode
            ? "Back to bag"
            : "Recover (" + std::to_string(snap.recovery.size()) + ")");
    RecoveryButton->SetEnabled(RecoveryMode || !snap.recovery.empty());
    SortButton->SetLabel(RecoveryMode ? "Next" : "Sort");
    DropButton->SetEnabled(!RecoveryMode);
    InventoryHint->SetText(
        RecoveryMode
            ? "Recovery: click to withdraw.\nUnknown definitions stay safe "
              "until restored."
            : "Click: move | Right: split/place 1 | Shift: quick | Ctrl: pin");
  }
  while (EffectLabels.size() < snap.effects.size() + 1)
  {
    auto label = std::make_unique<UGuiLabel>(&Context->GetTheme(), "");
    EffectLabels.push_back(label.get());
    StatsPanel->AddChild(std::move(label));
  }
  for (size_t i = 0; i < EffectLabels.size(); ++i)
  {
    auto *label = EffectLabels[i];
    label->SetVisible(i <= snap.effects.size());
    if (i == 0)
      label->SetText("Movement x" + std::to_string(snap.movementMultiplier) +
                     " | Fleshy damage: " +
                     std::to_string(snap.protection.Get("fleshy")) + "%");
    else if (i - 1 < snap.effects.size())
    {
      const auto &effect = snap.effects[i - 1];
      label->SetText(effect.DefId + " x" + std::to_string(effect.Stacks) +
                     "  " +
                     std::to_string(int(std::ceil(effect.RemainingSec))) + "s");
    }
  }
  Relayout();
  RefreshTooltip(snap);
  PreviewValid = snap.valid && !snap.typeId.empty();
  PreviewTypeId = snap.typeId;
  PreviewSkinId = EditingAppearance ? DraftSkin : snap.skinId;
  RefreshAppearanceControls();
  RenderCharacterPreview();
}

void UCharacterSheetScreen::RefreshTooltip(const CharacterStatsSnapshot &snap)
{
  if (Tooltip)
  {
    Tooltip->SetVisible(false);
    InventoryEntryRef hover;
    bool found = false;
    size_t index;
    auto recoveryHover = [&]
    {
      if (!RecoveryMode)
        return false;
      for (size_t i = 0; i < BagSlots.size(); ++i)
        if (BagSlots[i]->GetBounds().Contains(PointerX, PointerY))
        {
          index = i;
          return true;
        }
      return false;
    };
    if (PickBackpackSlot(PointerX, PointerY, index) || recoveryHover())
    {
      hover = RecoveryMode ? (RecoveryOffset + index < snap.recovery.size()
                                  ? snap.recovery[RecoveryOffset + index]
                                  : InventoryEntryRef{})
                           : snap.slots[index];
      found = !hover.empty;
    }
    auto *session = dynamic_cast<UGameSession *>(Stats);
    auto world = session ? session->GetWorld() : nullptr;
    if (!found && DollPanel->IsVisible() && world)
      if (auto *actor = world->GetControlledCreature())
      {
        for (size_t i = 0; i < ArmorSlots.size(); ++i)
          if (ArmorSlots[i]->GetBounds().Contains(PointerX, PointerY))
          {
            hover = actor->GetInventory().GetEquippedArmor(i);
            found = !hover.empty;
          }
        if (ToolSlots[0]->GetBounds().Contains(PointerX, PointerY))
        {
          auto *active = actor->GetInventory().GetActiveEntryRef();
          if (active)
          {
            hover = *active;
            found = !hover.empty;
          }
        }
        if (ToolSlots[1]->GetBounds().Contains(PointerX, PointerY))
        {
          hover = actor->GetInventory().GetEquippedOffhand();
          found = !hover.empty;
        }
      }
    if (found && world)
    {
      auto *defs = world->GetItemDefinitionStorage();
      auto *def = hover.kind == InventoryEntryKind::Item && defs
                      ? defs->Get(hover.Id)
                      : nullptr;
      std::string text = BuildItemTooltipText(def ? def->DisplayName : hover.Id,
                                              def, hover.wear, hover.broken);
      text += "\nCount: " + (hover.IsCatalogReference()
                                 ? std::string("Unlimited")
                                 : std::to_string(hover.count));
      std::string throwReason;
      if (hover.kind == InventoryEntryKind::Block ||
          hover.kind == InventoryEntryKind::Item)
        text +=
            UWorldItemSystem::Instance().CanThrow(*world, hover, throwReason)
                ? "\nR: charge and throw"
                : "\n" + throwReason;
      if (!hover.contentsId.empty())
        text += "\n" + hover.contentsId + ": " +
                std::to_string(hover.contentsUnits);
      if (def && !def->Armor.Slots.empty())
        text +=
            "\nFits: " + def->Armor.Slots.front() + " | Shift-click to equip";
      if (def && !def->Armor.Slots.empty() && !hover.broken)
        if (auto *actor = world->GetControlledCreature())
        {
          const char *names[] = {"head", "chest", "legs", "feet"};
          for (size_t slot = 0; slot < 4; ++slot)
            if (def->Armor.Slots.front() == names[slot])
            {
              auto trial = actor->GetInventory();
              if (trial.EquipArmor(slot, hover, *defs))
              {
                const auto current = actor->GetArmorGroups().Get("fleshy");
                const auto old =
                    actor->GetInventory()
                            .GetEquippedArmorGroups()
                            .Ratings.count("fleshy")
                        ? actor->GetInventory().GetEquippedArmorGroups().Get(
                              "fleshy")
                        : 100;
                const auto next = trial.GetEquippedArmorGroups().Get("fleshy");
                const int projected =
                    old > 0 ? int(std::round(float(current) * next / old))
                            : current;
                text += "\nDamage taken: " + std::to_string(current) +
                        "% -> ~" + std::to_string(projected) + "%";
              }
            }
        }
      GuiRect tooltipArea{0, 0, ViewportW, ViewportH};
      if (Hosted)
      {
        tooltipArea = PlayerWindowLayout::Compute(ViewportW, ViewportH, Metrics,
                                                  GetContentOffsetX(),
                                                  GetContentOffsetY())
                          .Window;
        tooltipArea.Y += Context->GetTheme().TitleBarHeight;
        tooltipArea.H -= Context->GetTheme().TitleBarHeight;
      }
      LayoutTooltipNearPointer(
          *Tooltip, text, PointerX, PointerY, tooltipArea, Context->GetTheme(),
          MeasureTooltipTextWidth(text, Context->GetTheme(),
                                  &Context->GetRenderer()));
    }
  }
}

void UCharacterSheetScreen::RunInventoryQa(const std::string &path)
{
  nlohmann::json report;
  report["layouts"] = nlohmann::json::array();
  bool passed = true;
  const auto scale = Context->GetUiScale();
  const int width = ViewportW, height = ViewportH;
  SetVisible(true);
  for (auto size :
       std::array<std::pair<int, int>, 2>{{{1280, 720}, {1920, 1080}}})
    for (float ui : {1.f, 1.5f, 2.f})
    {
      Context->ApplyUiScale(ui);
      OnMetricsChanged(Context->GetMetrics());
      OnViewportChanged(size.first, size.second);
      for (int section = 0; section < 4; ++section)
      {
        SetSection(section);
        Relayout();
        bool ok = true;
        int controls = 0;
        auto test = [&](UGuiWidget *widget)
        {
          auto b = widget->GetBounds();
          ++controls;
          ok = ok && b.W > 0 && b.H > 0 && b.X >= 0 && b.Y >= 0 &&
               b.X + b.W <= size.first && b.Y + b.H <= size.second &&
               Root->HitTest(b.X + b.W / 2, b.Y + b.H / 2) == widget;
        };
        if (section < 2)
        {
          for (auto *slot : BagSlots)
            test(slot);
          test(Search);
          test(SortButton);
          test(DropButton);
        }
        if (section == 1)
        {
          for (auto *slot : ArmorSlots)
            test(slot);
          for (auto *slot : ToolSlots)
            test(slot);
        }
        if (section == 3)
          for (auto *button : AppearanceButtons)
            test(button);
        report["layouts"].push_back({{"width", size.first},
                                     {"height", size.second},
                                     {"scale", ui},
                                     {"section", section},
                                     {"controls", controls},
                                     {"passed", ok}});
        passed = passed && ok;
      }
    }
  Context->ApplyUiScale(scale);
  OnMetricsChanged(Context->GetMetrics());
  OnViewportChanged(width, height);
  bool activeRowHandDrag = false;
  if (auto *session = dynamic_cast<UGameSession *>(Stats))
    if (auto *actor = session->GetWorld()->GetControlledCreature())
    {
      auto &inventory = actor->GetInventory();
      const auto oldBar = inventory.GetActiveBarIndex(),
                 oldSlot = inventory.GetActiveSlotIndex();
      inventory.SetActiveSlot(2, 1);
      SetSection(1);
      RefreshLabels();
      auto bounds = ToolSlots[0]->GetBounds();
      GuiMouseEvent event;
      event.X = bounds.X + bounds.W / 2;
      event.Y = bounds.Y + bounds.H / 2;
      ToolSlots[0]->OnMouseDown(event);
      event.X += 32;
      ToolSlots[0]->OnMouseMove(event);
      activeRowHandDrag = session->IsDragging() &&
                          session->GetDrag().source.bar == 2 &&
                          session->GetDrag().source.slot == 1 &&
                          session->GetDrag().entry.instanceId ==
                              inventory.GetSlot(21).instanceId;
      session->CancelDrag();
      ToolSlots[0]->ClearPressState();
      inventory.SetActiveSlot(oldBar, oldSlot);
    }
  report["active_row_hand_drag"] = activeRowHandDrag;
  passed = passed && activeRowHandDrag;
  SetSection(0);
  report["passed"] = passed;
  std::ofstream output(path);
  output << report.dump(2) << '\n';
}

void UCharacterSheetScreen::RunAppearanceQa(const std::string &reportPath)
{
  if (!Stats || Stats->GetCharacterStatsSnapshot().typeId != "human")
    throw std::runtime_error("Appearance UI QA requires controlled human");
  const auto original = Stats->GetCharacterStatsSnapshot();
  SetVisible(true);
  if (Hosted)
    SetSection(3);
  else
    AppearanceToggle->Activate();
  AppearanceButtons[0]->Activate();
  AppearanceButtons[1]->Activate();
  AppearanceButtons[2]->Activate();
  AppearanceButtons[2]->Activate();
  AppearanceButtons[3]->Activate();
  const auto desired = DraftAppearance;
  const auto outfit = DraftSkin;
  const bool draftIsolated =
      Stats->GetCharacterStatsSnapshot().humanAppearance.Key() ==
          original.humanAppearance.Key() &&
      Stats->GetCharacterStatsSnapshot().skinId == original.skinId;
  const bool previewWorks = PreviewTexture != 0;
  AppearanceButtons[4]->Activate();
  auto applied = Stats->GetCharacterStatsSnapshot();
  const bool appliedOk = applied.humanAppearance.Key() == desired.Key() &&
                         applied.skinId == outfit;
  bool userSaved = false;
  if (auto *session = dynamic_cast<UGameSession *>(Stats))
  {
    const auto world = session->GetWorld();
    const std::string path = reportPath + ".users.json";
    UWorldPersistence persistence;
    persistence.SaveUsers(*world, path);
    nlohmann::json saved;
    std::ifstream in(path);
    in >> saved;
    const auto &row = saved.at(world->GetCurrentUserName());
    userSaved =
        row.at("human_appearance").get<HumanAppearance>().Key() ==
            desired.Key() &&
        row.value("selected_skin_id", "") == outfit &&
        world->GetCurrentUser()->GetHumanAppearance().Key() == desired.Key();
  }
  if (Hosted)
    SetSection(3);
  else
    AppearanceToggle->Activate();
  AppearanceButtons[2]->Activate();
  AppearanceButtons[5]->Activate();
  const bool cancelled =
      Stats->GetCharacterStatsSnapshot().humanAppearance.Key() ==
          desired.Key() &&
      Stats->GetCharacterStatsSnapshot().skinId == outfit;
  nlohmann::json layouts = nlohmann::json::array();
  if (Hosted)
    SetSection(3);
  else
    AppearanceToggle->Activate();
  for (auto size :
       std::array<std::pair<int, int>, 2>{{{1280, 720}, {640, 480}}})
  {
    OnViewportChanged(size.first, size.second);
    bool valid = true;
    for (auto *b : AppearanceButtons)
    {
      const auto rect = b->GetBounds();
      valid =
          valid && rect.W > 0 && rect.H > 0 && rect.X >= 0 && rect.Y >= 0 &&
          rect.X + rect.W <= size.first && rect.Y + rect.H <= size.second &&
          Root->HitTestFocusable(rect.X + rect.W / 2, rect.Y + rect.H / 2) == b;
    }
    layouts.push_back({{"width", size.first},
                       {"height", size.second},
                       {"controls_reachable", valid}});
  }
  const bool restored =
      Stats->ApplyHumanAppearance(original.humanAppearance, original.skinId);
  EditingAppearance = false;
  ResetAppearanceDraft();
  OnViewportChanged(1280, 720);
  RefreshLabels();
  if (Hosted)
    SetSection(3);
  else
    AppearanceToggle->Activate();
  AppearanceButtons[0]->Activate();
  AppearanceButtons[2]->Activate();
  AppearanceButtons[2]->Activate();
  bool passed = draftIsolated && previewWorks && appliedOk && cancelled &&
                restored && userSaved;
  for (const auto &layout : layouts)
    passed = passed && layout["controls_reachable"].get<bool>();
  nlohmann::json report = {{"passed", passed},
                           {"draft_isolated", draftIsolated},
                           {"preview_rendered", previewWorks},
                           {"apply", appliedOk},
                           {"cancel", cancelled},
                           {"original_selection_restored", restored},
                           {"user_save_serialized", userSaved},
                           {"layouts", layouts}};
  std::ofstream file(reportPath);
  file << report.dump(2) << '\n';
  file.close();
  if (!passed)
    throw std::runtime_error("Appearance UI QA failed");
}

void UCharacterSheetScreen::ResetAppearanceDraft()
{
  DraftInitialized = true;
  if (!Stats)
    return;
  const auto snap = Stats->GetCharacterStatsSnapshot();
  DraftOwnerId = snap.OwnerId;
  DraftBase = snap.humanAppearance.Key() + "|" + snap.skinId;
  DraftAppearance = snap.humanAppearance;
  DraftSkin = snap.skinId;
}
void UCharacterSheetScreen::RefreshAppearanceControls()
{
  if (!Stats || !AppearanceToggle)
    return;
  const bool human = Stats->GetCharacterStatsSnapshot().typeId == "human";
  AppearanceToggle->SetVisible(!Hosted && human);
  if (!human && EditingAppearance)
  {
    EditingAppearance = false;
    Relayout();
  }
  if (AppearanceButtons.size() != 6)
    return;
  AppearanceButtons[0]->SetLabel(
      DraftAppearance.bodyVariant == "female" ? "Body: female" : "Body: male");
  AppearanceButtons[1]->SetLabel(DraftSkin == "human_guard" ? "Outfit: guard"
                                 : DraftSkin == "human_adventurer"
                                     ? "Outfit: traveler"
                                     : "Outfit: default");
  const std::array<const char *, 6> hair{"No hair", "Short",    "Bob",
                                         "Long",    "Ponytail", "Curly"};
  const std::array<const char *, 5> color{"Black", "Chestnut", "Brown", "Blond",
                                          "Ginger"};
  for (size_t i = 0; i < hair.size(); ++i)
    if (DraftAppearance.hairstyleId == HumanAppearance::Hairstyles[i])
      AppearanceButtons[2]->SetLabel(std::string("Hairstyle: ") + hair[i]);
  for (size_t i = 0; i < color.size(); ++i)
    if (DraftAppearance.hairColorId == HumanAppearance::Colors[i])
      AppearanceButtons[3]->SetLabel(std::string("Hair color: ") + color[i]);
  AppearanceButtons[4]->SetLabel("Apply");
  AppearanceButtons[5]->SetLabel("Cancel");
}

void UCharacterSheetScreen::RenderCharacterPreview()
{
  if (!PreviewViewport)
  {
    return;
  }
  if (!CreaturePreview || !PreviewValid)
  {
    if (PreviewTexture != 0)
    {
      glDeleteTextures(1, &PreviewTexture);
      PreviewTexture = 0;
    }
    PreviewViewport->SetPreviewTexture(0);
    return;
  }

  const GuiRect b = PreviewViewport->GetBounds();
  const int size = std::max(64, std::min(b.W, b.H));
  if (size <= 0 || PreviewTypeId.empty())
  {
    if (PreviewTexture != 0)
    {
      glDeleteTextures(1, &PreviewTexture);
      PreviewTexture = 0;
    }
    PreviewViewport->SetPreviewTexture(0);
    return;
  }

  std::array<WornArmorPreviewSlot, kWornArmorSlotCount> armorSlots{};
  std::string armorKey;
  std::string mainId;
  std::string offId;
  std::string wieldKey;
  if (Stats)
  {
    const CharacterStatsSnapshot snap = Stats->GetCharacterStatsSnapshot();
    for (size_t i = 0; i < kWornArmorSlotCount; ++i)
    {
      const auto &s = snap.equippedArmor[i];
      if (!s.itemId.empty() && !s.broken)
      {
        armorSlots[i].ItemId = s.itemId;
      }
      armorKey += s.itemId;
      armorKey.push_back('|');
      armorKey += s.broken ? '1' : '0';
      armorKey.push_back(';');
    }
    const auto &main = snap.equippedTools[0];
    const auto &off = snap.equippedTools[1];
    if (!main.itemId.empty() && !main.broken && !main.isBlock)
    {
      mainId = main.itemId;
    }
    if (!off.itemId.empty() && !off.broken && !off.isBlock)
    {
      offId = off.itemId;
    }
    wieldKey = mainId + "|" + offId;
  }

  const float yaw = PreviewViewport->GetYaw();
  const float pitch = PreviewViewport->GetPitch();
  const auto look = EditingAppearance
                        ? DraftAppearance
                        : Stats->GetCharacterStatsSnapshot().humanAppearance;
  const bool sameCache =
      look.Key() == LastCachedAppearance && PreviewTexture != 0 &&
      size == LastPreviewSize && PreviewTypeId == LastCachedTypeId &&
      PreviewSkinId == LastCachedSkinId && armorKey == LastCachedArmorKey &&
      wieldKey == LastCachedWieldKey &&
      std::abs(yaw - LastPreviewYaw) < 0.01f &&
      std::abs(pitch - LastPreviewPitch) < 0.01f;
  if (sameCache)
  {
    return;
  }

  const GLuint tex = CreaturePreview->RenderToUniqueTexture(
      PreviewTypeId, PreviewSkinId, size, yaw, pitch, 0.f, false, &armorSlots,
      mainId.empty() ? nullptr : &mainId, offId.empty() ? nullptr : &offId,
      &look);
  if (tex == 0)
  {
    return;
  }

  if (PreviewTexture != 0)
  {
    glDeleteTextures(1, &PreviewTexture);
  }
  PreviewTexture = tex;
  PreviewViewport->SetPreviewTexture(PreviewTexture);
  LastPreviewYaw = yaw;
  LastPreviewPitch = pitch;
  LastPreviewSize = size;
  LastCachedTypeId = PreviewTypeId;
  LastCachedSkinId = PreviewSkinId;
  LastCachedAppearance = look.Key();
  LastCachedArmorKey = armorKey;
  LastCachedWieldKey = wieldKey;
}

void UCharacterSheetScreen::Update(double /*dt*/)
{
  if (Visible)
  {
    if (Icons && Icons->PendingItemIcons())
      Icons->WarmupItemIcons(3);
    RefreshLabels();
  }
}

void UCharacterSheetScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

std::string UCharacterSheetScreen::GetSectionStateKey() const
{
  return (Search ? Search->GetText() : "") + "|" +
         std::to_string(RecoveryMode) + "|" + std::to_string(RecoveryOffset) +
         "|" + std::to_string(StatsScroll->GetScrollY()) + "|" +
         DraftAppearance.Key() + "|" + DraftSkin + "|" +
         std::to_string(PreviewViewport->GetYaw()) + "|" +
         std::to_string(PreviewViewport->GetPitch());
}
GuiRect UCharacterSheetScreen::GetSearchBounds() const
{
  return Search->GetBounds();
}
GuiRect UCharacterSheetScreen::GetBackpackSlotBounds(size_t index) const
{
  return index < BagSlots.size() ? BagSlots[index]->GetBounds() : GuiRect{};
}
} // namespace cutum
