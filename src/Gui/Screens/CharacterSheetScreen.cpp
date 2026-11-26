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

#include "Render/Textures/GlTextureOwner.h"

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
  if (!visible && Widgets.Search)
    Widgets.Search->SetFocused(false);
  if (!visible)
    if (auto *session = dynamic_cast<UGameSession *>(Stats))
      session->CancelDrag();
  if (visible != Visible)
    EditingAppearance = Section == 3;
  Visible = visible;
  if (Root)
    Root->SetVisible(visible);
  if (Visible)
    RefreshLabels();
}

GuiRect UCharacterSheetScreen::GetMainSlotBounds() const
{
  return Widgets.ToolSlots[0] ? Widgets.ToolSlots[0]->GetBounds() : GuiRect{};
}
GuiRect UCharacterSheetScreen::GetDropButtonBounds() const
{
  return Widgets.DropButton ? Widgets.DropButton->GetBounds() : GuiRect{};
}
GuiRect UCharacterSheetScreen::GetDeleteButtonBounds() const
{
  return Widgets.DeleteButton ? Widgets.DeleteButton->GetBounds() : GuiRect{};
}
void UCharacterSheetScreen::Toggle() { SetVisible(!Visible); }

bool UCharacterSheetScreen::IsSearchFocused() const
{
  return Widgets.Search && Widgets.Search->IsFocused();
}
bool UCharacterSheetScreen::RouteTextKey(const GuiKeyEvent &e)
{
  return IsSearchFocused() ? Widgets.Search->OnKey(e) : false;
}
bool UCharacterSheetScreen::RouteTextChar(const GuiCharEvent &e)
{
  return IsSearchFocused() ? Widgets.Search->OnChar(e) : false;
}
void UCharacterSheetScreen::BuildEquipmentPanel(PageWidgets &page, UGuiPanel &body,
                                                const GuiTheme &theme)
{
  // Paper-doll panel (preview + equipment).
  {
    auto doll = std::make_unique<UGuiPanel>(&theme);
    doll->ReserveChildren(2 + page.ArmorSlots.size() + page.ToolSlots.size());
    page.DollPanel = doll.get();
    page.DollPanel->SetDrawBackground(true);

    auto eqHead = std::make_unique<UGuiLabel>(&theme, "Equipment");
    page.EquipmentHeader = eqHead.get();
    page.DollPanel->AddChild(std::move(eqHead));

    auto preview = std::make_unique<UGuiPreviewViewport>(&theme);
    page.PreviewViewport = preview.get();
    page.PreviewViewport->SetOnRotationChanged([this](float, float)
    {
      if (Stats)
        try { RenderCharacterPreview(Stats->GetCharacterLookSnapshot()); }
        catch (const std::bad_alloc &) { /* Retry on the next refresh. */ }
    });
    page.DollPanel->AddChild(std::move(preview));

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
      page.ArmorSlots[i] = slot.get();
      page.DollPanel->AddChild(std::move(slot));
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
              address.bar = session->GetActiveHotbarRow();
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
              address.bar = session->GetActiveHotbarRow();
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
              const auto row = session->GetActiveHotbarRow();
              const auto index = row * 10 + session->GetSelectedSlot(row);
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
      page.ToolSlots[i] = slot.get();
      page.DollPanel->AddChild(std::move(slot));
    }

    body.AddChild(std::move(doll));
  }
}

void UCharacterSheetScreen::Build(UGuiContext &ctx)
{
  const GuiTheme &theme = ctx.GetTheme();
  const int rendererW = ctx.GetRenderer().GetWindowWidth();
  const int rendererH = ctx.GetRenderer().GetWindowHeight();
  const int width = Root || rendererW <= 0 ? ViewportW : rendererW;
  const int height = Root || rendererH <= 0 ? ViewportH : rendererH;
  PageWidgets page;

  auto root = std::make_unique<UGuiPanel>(&theme);
  root->SetDrawBackground(false);
  root->SetVisible(Visible);
  root->ReserveChildren(2);

  auto window = std::make_unique<UGuiWindow>(&theme, "Player");
  window->ReserveChildren(1);
  page.Window = window.get();

  auto body = std::make_unique<UGuiPanel>(&theme);
  page.BodyPanel = body.get();
  body->SetDrawBackground(false);
  body->ReserveChildren(14);

  auto meta = std::make_unique<UGuiLabel>(&theme, "");
  page.TitleMeta = meta.get();
  body->AddChild(std::move(meta));

  auto mode = std::make_unique<UGuiLabel>(&theme, "");
  page.ModeLabel = mode.get();
  page.ModeLabel->SetUseSecondaryColor(true);
  body->AddChild(std::move(mode));

  auto tabs = std::make_unique<UGuiTabBar>(&theme);
  page.Sections = tabs.get();
  page.Sections->SetTabs(
      {"Backpack", "Equipment", "Status", "Appearance", "Catalog"});
  page.Sections->SetActiveTab(Section);
  page.Sections->SetOnTabChanged(
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
  page.Search = search.get();
  page.Search->SetClipboard(ctx.GetClipboard());
  page.Search->SetOnEdited([this] { RefreshLabels(); });
  body->AddChild(std::move(search));
  auto sort = std::make_unique<UGuiButton>(&theme, "Sort");
  page.SortButton = sort.get();
  page.SortButton->SetOnClick(
      [this]
      {
        if (RecoveryMode)
        {
          const auto size = Stats->GetCharacterRecoveryCount();
          RecoveryOffset = RecoveryOffset + 40 < size ? RecoveryOffset + 40 : 0;
        }
        else if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->SortInventory();
        RefreshLabels();
      });
  body->AddChild(std::move(sort));
  auto drop = std::make_unique<UGuiButton>(&theme, "Drop");
  page.DropButton = drop.get();
  page.DropButton->SetOnClick(
      [this]
      {
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->DiscardInventory();
        RefreshLabels();
      });
  body->AddChild(std::move(drop));
  auto remove = std::make_unique<UGuiButton>(&theme, "Clear template");
  page.DeleteButton = remove.get();
  remove->SetOnClick(
      [this]
      {
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          session->DeleteCreativeAssignment();
        RefreshLabels();
      });
  body->AddChild(std::move(remove));
  auto recovery = std::make_unique<UGuiButton>(&theme, "Recover");
  page.RecoveryButton = recovery.get();
  page.RecoveryButton->SetOnClick(
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
  page.InventoryHint = hint.get();
  body->AddChild(std::move(hint));
  auto toggle = std::make_unique<UGuiButton>(&theme, "Appearance");
  page.AppearanceToggle = toggle.get();
  toggle->SetOnClick([this] { SetSection(EditingAppearance ? 1 : 3); });
  body->AddChild(std::move(toggle));
  auto customization = std::make_unique<UGuiPanel>(&theme);
  customization->ReserveChildren(page.AppearanceButtons.size());
  page.AppearancePanel = customization.get();
  for (int row = 0; row < 6; ++row)
  {
    auto button = std::make_unique<UGuiButton>(&theme, "");
    page.AppearanceButtons[row] = button.get();
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
            DraftInitialized = false;
            SetSection(1);
            return;
          }
          RefreshLabels();
        });
    customization->AddChild(std::move(button));
  }
  body->AddChild(std::move(customization));

  BuildEquipmentPanel(page, *body, theme);

  {
    auto bag = std::make_unique<UGuiPanel>(&theme);
    bag->ReserveChildren(page.BagSlots.size());
    page.BagPanel = bag.get();
    page.BagPanel->SetDrawBackground(true);
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
      page.BagSlots[i] = slot.get();
      page.BagPanel->AddChild(std::move(slot));
    }
    body->AddChild(std::move(bag));
  }

  // Stats panel (vitals + attributes).
  {
    auto scroll = std::make_unique<UGuiScrollView>(&theme);
    page.StatsScroll = scroll.get();
    auto *stats = &scroll->Content();
    page.StatsPanel = stats;
    page.StatsPanel->SetDrawBackground(true);
    page.StatsPanel->ReserveChildren(FixedStatusRows);

    auto vitHead = std::make_unique<UGuiLabel>(&theme, "Vitals");
    page.VitalsHeader = vitHead.get();
    page.StatsPanel->AddChild(std::move(vitHead));

    for (int i = 0; i < 7; ++i)
    {
      auto lab = std::make_unique<UGuiLabel>(&theme, "");
      page.VitalLabels[i] = lab.get();
      page.StatsPanel->AddChild(std::move(lab));
    }

    auto attrHead = std::make_unique<UGuiLabel>(&theme, "Attributes");
    page.AttrsHeader = attrHead.get();
    page.StatsPanel->AddChild(std::move(attrHead));

    for (int i = 0; i < 7; ++i)
    {
      auto lab = std::make_unique<UGuiLabel>(&theme, "");
      page.AttrLabels[i] = lab.get();
      page.StatsPanel->AddChild(std::move(lab));
    }

    body->AddChild(std::move(scroll));
  }

  window->AddChild(std::move(body));
  root->AddChild(std::move(window));
  auto tooltip = std::make_unique<UGuiLabel>(&theme, "");
  page.Tooltip = tooltip.get();
  tooltip->SetVisible(false);
  tooltip->SetZOrder(1000);
  root->AddChild(std::move(tooltip));
  if (Root)
  {
    page.Search->SetText(Widgets.Search->GetText());
    page.TitleMeta->SetText(Widgets.TitleMeta->GetText());
    page.ModeLabel->SetText(Widgets.ModeLabel->GetText());
    for (size_t i = 0; i < page.VitalLabels.size(); ++i)
      page.VitalLabels[i]->SetText(Widgets.VitalLabels[i]->GetText());
    for (size_t i = 0; i < page.AttrLabels.size(); ++i)
      page.AttrLabels[i]->SetText(Widgets.AttrLabels[i]->GetText());
    page.PreviewViewport->SetAngles(Widgets.PreviewViewport->GetYaw(),
                                    Widgets.PreviewViewport->GetPitch());
    const auto &rows = Widgets.StatsPanel->GetChildren();
    page.StatsPanel->ReserveChildren(rows.size());
    for (size_t i = FixedStatusRows; i < rows.size(); ++i)
      page.StatsPanel->AddChild(std::make_unique<UGuiLabel>(
          &theme, static_cast<const UGuiLabel &>(*rows[i]).GetText()));
  }
  page.Sections->SetTabEnabled(4, Stats && Stats->GetWorldGameMode() == WorldGameMode::Creative);
  page.PreviewViewport->SetPreviewTexture(PreviewTexture);
  Relayout(page, *root, width, height);
  if (Root)
    page.StatsScroll->SetScrollY(Widgets.StatsScroll->GetScrollY());

  Widgets = page;
  Root = std::move(root);
  ViewportW = width;
  ViewportH = height;
}

void UCharacterSheetScreen::SetSection(int section)
{
  Section = std::clamp(section, 0, 3);
  EditingAppearance = Section == 3;
  if (Widgets.Sections && Widgets.Sections->GetActiveTab() != Section)
    Widgets.Sections->SetActiveTab(Section);
  if (Widgets.Search)
    Widgets.Search->SetFocused(false);
  if (auto *session = dynamic_cast<UGameSession *>(Stats))
    session->CancelDrag();
  Relayout();
  RefreshLabels();
}
bool UCharacterSheetScreen::IsEditingText() const
{
  return Widgets.Search && Widgets.Search->IsFocused();
}
bool UCharacterSheetScreen::PickBackpackSlot(int x, int y, size_t &slot) const
{
  if (!Root || !Visible || Section > 1 || RecoveryMode)
    return false;
  for (size_t i = 0; i < Widgets.BagSlots.size(); ++i)
    if (Widgets.BagSlots[i]->IsVisible() && Widgets.BagSlots[i]->GetBounds().Contains(x, y))
    {
      slot = i;
      return true;
    }
  return false;
}
void UCharacterSheetScreen::Relayout()
{
  if (Root)
    Relayout(Widgets, *Root, ViewportW, ViewportH);
}

void UCharacterSheetScreen::Relayout(PageWidgets &page, UGuiWidget &root,
                                     int width, int height)
{
  root.SetBounds({0, 0, width, height});
  const int pad = Scaled(8), line = Scaled(24), tabH = Scaled(32);
  const int w = std::max(300, std::min(width - pad * 2, Scaled(1080)));
  const int h = std::max(300, std::min(height - pad * 2, Scaled(660)));
  page.Window->SetBounds({(width - w) / 2, (height - h) / 2, w, h});
  auto c = page.Window->GetClientArea();
  page.BodyPanel->SetBounds(c);
  const bool wideLayout = c.W >= Scaled(740);
  page.TitleMeta->SetBounds({c.X + pad, c.Y + pad, c.W - pad * 2, line});
  page.AppearanceToggle->SetVisible(true);
  page.AppearanceToggle->SetBounds(
      {c.X + c.W - Scaled(135), c.Y + pad, Scaled(125), line});
  page.TitleMeta->SetBounds({c.X + pad, c.Y + pad, c.W - Scaled(155), line});
  page.ModeLabel->SetBounds({c.X + pad, c.Y + pad + line, c.W - pad * 2, line});
  page.Sections->SetBounds({c.X + pad, c.Y + pad + line * 2, c.W - pad * 2, tabH});
  const auto shared = PlayerWindowLayout::Compute(
      width, height, Metrics, GetContentOffsetX(), GetContentOffsetY());
  if (Hosted)
  {
    page.Window->SetChromeVisible(false);
    page.Window->SetBounds(shared.Body);
    page.BodyPanel->SetBounds(shared.Body);
    page.TitleMeta->SetVisible(false);
    page.ModeLabel->SetVisible(false);
    page.Sections->SetVisible(false);
    page.AppearanceToggle->SetVisible(false);
  }
  const int top = Hosted ? shared.Body.Y : c.Y + pad + line * 2 + tabH + pad;
  const int contentH =
      Hosted ? shared.Body.H : std::max(100, c.Y + c.H - top - pad);
  const int dollW = Hosted ? shared.Preview.W
                           : ((wideLayout || Section == 1 || Section == 3)
                                  ? std::min(Scaled(320), c.W / 3)
                                  : 0);
  const int mainX = Hosted ? shared.Content.X
                           : c.X + pad + (dollW ? dollW + pad : 0),
            mainW = Hosted ? shared.Content.W
                           : c.W - pad * 2 - (dollW ? dollW + pad : 0);
  page.DollPanel->SetVisible(dollW > 0 || Section == 3);
  page.DollPanel->SetBounds(
      Hosted ? shared.Preview
             : GuiRect{c.X + pad, top, dollW ? dollW : c.W / 3, contentH});
  const bool bagVisible = Section < 2;
  page.BagPanel->SetVisible(bagVisible);
  page.Search->SetVisible(bagVisible);
  page.SortButton->SetVisible(bagVisible);
  page.RecoveryButton->SetVisible(bagVisible);
  page.DropButton->SetVisible(bagVisible);
  page.InventoryHint->SetVisible(bagVisible && mainW >= Scaled(650));
  page.Search->SetBounds(
      {mainX, top, std::max(40, mainW - Scaled(190)), line + pad});
  page.SortButton->SetBounds(
      {mainX + mainW - Scaled(182), top, Scaled(65), line + pad});
  page.RecoveryButton->SetBounds(
      {mainX + mainW - Scaled(110), top, Scaled(110), line + pad});
  page.DropButton->SetBounds({mainX + mainW - Scaled(75),
                         top + contentH - line - pad, Scaled(75), line + pad});
  auto dropBounds = page.DropButton->GetBounds();
  page.DeleteButton->SetBounds(
      {dropBounds.X - Scaled(150), dropBounds.Y, Scaled(145), dropBounds.H});
  page.DeleteButton->SetVisible(bagVisible && Stats &&
                           Stats->GetWorldGameMode() == WorldGameMode::Creative);
  const int gridTop = top + line + pad * 2;
  const int slot =
      std::max(20, std::min((mainW - pad) / 10 - pad / 2,
                            (contentH - (line + pad) * 2 - pad * 4) / 4 - pad));
  page.BagPanel->SetBounds({mainX, gridTop, mainW, 4 * (slot + pad)});
  for (size_t i = 0; i < 40; ++i)
    page.BagSlots[i]->SetBounds({mainX + pad / 2 + int(i % 10) * (slot + pad / 2),
                            gridTop + pad / 2 + int(i / 10) * (slot + pad),
                            slot, slot});
  page.InventoryHint->SetBounds({mainX, top + contentH - line - pad,
                            std::max(40, mainW - Scaled(235)), line * 2});
  page.StatsScroll->SetVisible(Section == 2);
  page.StatsScroll->SetBounds({mainX, top, mainW, contentH});
  auto labelHeight = [&](UGuiLabel *label)
  {
    auto b = label->GetBounds();
    b.H = line;
    label->SetBounds(b);
  };
  labelHeight(page.VitalsHeader);
  labelHeight(page.AttrsHeader);
  for (auto *label : page.VitalLabels)
    labelHeight(label);
  for (auto *label : page.AttrLabels)
    labelHeight(label);
  page.StatsScroll->LayoutContent(pad / 2, pad);
  page.AppearancePanel->SetBounds({mainX, top, mainW, contentH});
  page.AppearancePanel->SetVisible(EditingAppearance);
  for (size_t i = 0; i < page.AppearanceButtons.size(); ++i)
    page.AppearanceButtons[i]->SetBounds({mainX + pad,
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
  page.PreviewViewport->SetBounds({px, py, preview, preview});
  if (EditingAppearance)
    page.PreviewViewport->SetBounds({left + pad, top + line, dw - pad * 2,
                                std::min(contentH - line, dw * 2)});
  page.EquipmentHeader->SetBounds({left + pad, top + pad, dw - pad * 2, line});
  page.EquipmentHeader->SetVisible(!EditingAppearance);
  const std::array<GuiRect, 4> armor{
      {{px + (preview - eqSlot) / 2, py - eqSlot - pad, eqSlot, eqSlot},
       {px - eqSlot - pad, py + (preview - eqSlot) / 2, eqSlot, eqSlot},
       {px + preview + pad, py + (preview - eqSlot) / 2, eqSlot, eqSlot},
       {px + (preview - eqSlot) / 2, py + preview + pad, eqSlot, eqSlot}}};
  for (size_t i = 0; i < 4; ++i)
  {
    page.ArmorSlots[i]->SetBounds(armor[i]);
    page.ArmorSlots[i]->SetVisible(!EditingAppearance);
  }
  for (size_t i = 0; i < 2; ++i)
  {
    page.ToolSlots[i]->SetBounds({left + pad + int(i) * (dw / 2),
                             py + preview + eqSlot + pad * 3, eqSlot, eqSlot});
    page.ToolSlots[i]->SetVisible(!EditingAppearance);
  }
}

bool UCharacterSheetScreen::PickArmorSlot(int x, int y, size_t &outSlot) const
{
  if (EditingAppearance || !Widgets.DollPanel->IsVisible())
    return false;
  for (size_t i = 0; i < Widgets.ArmorSlots.size(); ++i)
  {
    if (Widgets.ArmorSlots[i] && PointInBounds(x, y, Widgets.ArmorSlots[i]->GetBounds()))
    {
      outSlot = i;
      return true;
    }
  }
  return false;
}

bool UCharacterSheetScreen::PickMainSlot(int x, int y) const
{
  if (EditingAppearance || !Widgets.DollPanel->IsVisible())
    return false;
  if (!Widgets.ToolSlots[0])
  {
    return false;
  }
  return PointInBounds(x, y, Widgets.ToolSlots[0]->GetBounds());
}

bool UCharacterSheetScreen::PickOffhandSlot(int x, int y) const
{
  if (EditingAppearance || !Widgets.DollPanel->IsVisible())
    return false;
  if (!Widgets.ToolSlots[1])
  {
    return false;
  }
  return PointInBounds(x, y, Widgets.ToolSlots[1]->GetBounds());
}

bool UCharacterSheetScreen::PickDoll(int x, int y) const
{
  return Widgets.PreviewViewport && PointInBounds(x, y, Widgets.PreviewViewport->GetBounds());
}

void UCharacterSheetScreen::RefreshLabels() try
{
  if (!Stats || !Root)
    return;
  const auto snap = Stats->GetCharacterPageSnapshot(
      RecoveryOffset, RecoveryMode ? Widgets.BagSlots.size() : 0);
  auto draftBase = snap.humanAppearance.Key() + "|" + snap.skinId;
  if (!DraftInitialized || DraftOwnerId != snap.OwnerId ||
      DraftBase != draftBase)
    ResetAppearanceDraft(snap, std::move(draftBase));
  if (Widgets.TitleMeta)
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
    Widgets.TitleMeta->SetText(oss.str());
  }
  Widgets.Sections->SetTabEnabled(4, snap.gameMode == WorldGameMode::Creative);
  if (Widgets.ModeLabel)
  {
    if (snap.gameMode == WorldGameMode::Creative)
    {
      Widgets.ModeLabel->SetText(
          "Creative: instant mining + loot; no wear; vitals frozen");
    }
    else
    {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "Mode: Survival / %s",
                    WorldDifficultyToString(snap.difficulty));
      Widgets.ModeLabel->SetText(buf);
    }
  }
  if (snap.valid)
  {
    const auto &v = snap.vitals;
    Widgets.VitalLabels[0]->SetText(FormatBar("Health", v.health, v.maxHealth));
    Widgets.VitalLabels[1]->SetText(FormatBar("Satiety", v.satiety, v.maxSatiety));
    Widgets.VitalLabels[2]->SetText(FormatBar("Thirst", v.thirst, v.maxThirst));
    Widgets.VitalLabels[3]->SetText(FormatBar("Fatigue", v.fatigue, v.maxFatigue));
    Widgets.VitalLabels[4]->SetText(FormatBar("Breath", v.breath, v.maxBreath));
    char wounds[64];
    std::snprintf(wounds, sizeof(wounds), "Fatal wounds: %d / %d",
                  v.fatalWounds, v.maxFatalWounds);
    Widgets.VitalLabels[5]->SetText(wounds);
    char armor[48];
    std::snprintf(armor, sizeof(armor), "Armor: %.0f", v.armor);
    Widgets.VitalLabels[6]->SetText(armor);
  }
  if (snap.valid)
  {
    const auto &a = snap.attributes;
    auto setAttr = [](UGuiLabel *lab, const char *name, int value)
    {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%s: %d", name, value);
      lab->SetText(buf);
    };
    setAttr(Widgets.AttrLabels[0], "Strength", a.strength);
    setAttr(Widgets.AttrLabels[1], "Agility", a.agility);
    setAttr(Widgets.AttrLabels[2], "Endurance", a.endurance);
    setAttr(Widgets.AttrLabels[3], "Accuracy", a.accuracy);
    setAttr(Widgets.AttrLabels[4], "Intelligence", a.intelligence);
    setAttr(Widgets.AttrLabels[5], "Luck", a.luck);
    setAttr(Widgets.AttrLabels[6], "Perception", a.perception);
  }

  if (Icons)
  {
    for (size_t i = 0; i < Widgets.ArmorSlots.size(); ++i)
    {
      const auto &s = snap.equippedArmor[i];
      if (Widgets.ArmorSlots[i] && !s.itemId.empty())
      {
        Widgets.ArmorSlots[i]->SetIconTexture(Icons->GetItemIconTexture(s.itemId));
        Widgets.ArmorSlots[i]->SetWearProgress(s.wear);
        Widgets.ArmorSlots[i]->SetBroken(s.broken);
        Widgets.ArmorSlots[i]->SetDimmed(false);
      }
      else if (Widgets.ArmorSlots[i])
      {
        Widgets.ArmorSlots[i]->SetIconTexture(0);
        Widgets.ArmorSlots[i]->SetWearProgress(0.f);
        Widgets.ArmorSlots[i]->SetBroken(false);
        Widgets.ArmorSlots[i]->SetDimmed(false);
      }
    }

    for (size_t i = 0; i < Widgets.ToolSlots.size(); ++i)
    {
      const auto &s = snap.equippedTools[i];
      if (Widgets.ToolSlots[i])
      {
        const auto &active = snap.slots[snap.activeRow * 10 + snap.activeSlot];
        Widgets.ToolSlots[i]->SetCountHint(i == 0 && !active.empty &&
                                           !active.IsCatalogReference()
                                       ? std::to_string(active.count)
                                       : "");
      }
      if (Widgets.ToolSlots[i] && !s.itemId.empty())
      {
        GLuint tex = s.isBlock ? Icons->GetBlockIconTexture(s.itemId)
                               : Icons->GetItemIconTexture(s.itemId);
        if (auto *session = dynamic_cast<UGameSession *>(Stats))
          if (auto world = session->GetWorld())
            if (const auto *actor = world->GetControlledCreature())
            {
              const auto *entry =
                  i == 0 ? actor->GetInventory().GetActiveEntryRef()
                         : &actor->GetInventory().GetEquippedOffhand();
              if (entry)
                tex = Icons->GetEntryIcon(*entry);
            }
        Widgets.ToolSlots[i]->SetIconTexture(tex);
        Widgets.ToolSlots[i]->SetWearProgress(s.wear);
        Widgets.ToolSlots[i]->SetBroken(s.broken);
        Widgets.ToolSlots[i]->SetDimmed(false);
      }
      else if (Widgets.ToolSlots[i])
      {
        Widgets.ToolSlots[i]->SetIconTexture(0);
        Widgets.ToolSlots[i]->SetWearProgress(0.f);
        Widgets.ToolSlots[i]->SetBroken(false);
        Widgets.ToolSlots[i]->SetDimmed(false);
      }
    }
    const std::string_view query = Widgets.Search ? std::string_view(Widgets.Search->GetText()) : std::string_view{};
    const auto folded = [](unsigned char c) { return c == '_' ? ' ' : std::tolower(c); };
    for (size_t i = 0; i < Widgets.BagSlots.size(); ++i)
    {
      const InventoryEntryRef empty;
      const auto &entry = RecoveryMode
                              ? (i < snap.recovery.size()
                                     ? snap.recovery[i]
                                     : empty)
                              : snap.slots[i];
      auto *slot = Widgets.BagSlots[i];
      const auto tex = Icons->GetEntryIcon(entry);
      if (RecoveryMode)
        slot->SetLabel(entry.Id);
      else
        slot->SetLabel("");
      slot->SetIconTexture(tex);
      slot->SetWearProgress(entry.wear);
      slot->SetBroken(entry.broken);
      slot->SetCornerHint(!entry.empty && entry.pinned ? "*" : "");
      slot->SetCountHint(entry.empty || entry.IsCatalogReference()
                             ? ""
                             : std::to_string(entry.count));
      slot->SetDimmed(!query.empty() &&
          std::search(entry.Id.begin(), entry.Id.end(), query.begin(), query.end(),
                      [&](unsigned char left, unsigned char right) { return folded(left) == folded(right); }) == entry.Id.end());
      slot->SetSelected(!RecoveryMode && i / 10 == snap.activeRow &&
                        i % 10 == snap.activeSlot);
      slot->SetVisible(true);
    }
  }
  if (Widgets.RecoveryButton)
  {
    Widgets.RecoveryButton->SetLabel(
        RecoveryMode
            ? "Back to bag"
            : "Recover (" + std::to_string(snap.recoveryCount) + ")");
    Widgets.RecoveryButton->SetEnabled(RecoveryMode || snap.recoveryCount != 0);
    Widgets.SortButton->SetLabel(RecoveryMode ? "Next" : "Sort");
    Widgets.DropButton->SetEnabled(!RecoveryMode);
    Widgets.InventoryHint->SetText(
        RecoveryMode
            ? "Recovery: click to withdraw.\nUnknown definitions stay safe "
              "until restored."
            : "Click: move | Right: split/place 1 | Shift: quick | Ctrl: pin");
  }
  RefreshStatusRows(snap);
  Relayout();
  RefreshTooltip(snap);
  RefreshAppearanceControls(snap.typeId == "human");
  RenderCharacterPreview(snap);
}
catch (const std::bad_alloc &)
{
  // The visible page can retry the refresh on the next update.
}

void UCharacterSheetScreen::RefreshStatusRows(const CharacterPageSnapshot &snap)
{
  auto &children = Widgets.StatsPanel->Children;
  const size_t count = FixedStatusRows + 1 + snap.effects.size();
  std::vector<std::pair<size_t, std::unique_ptr<UGuiLabel>>> changed;
  const auto stage = [&](size_t index, std::string text)
  {
    if (index < children.size() &&
        static_cast<const UGuiLabel &>(*children[index]).GetText() == text)
      return;
    auto label = std::make_unique<UGuiLabel>(&Context->GetTheme(), std::move(text));
    label->UpdateLayout(Widgets.StatsPanel->GetBounds());
    changed.emplace_back(index, std::move(label));
  };
  stage(FixedStatusRows,
        "Movement x" + std::to_string(snap.movementMultiplier) +
            " | Fleshy damage: " + std::to_string(snap.fleshyProtection) + "%");
  for (size_t i = 0; i < snap.effects.size(); ++i)
  {
    const auto &effect = snap.effects[i];
    stage(FixedStatusRows + 1 + i,
          effect.DefId + " x" + std::to_string(effect.Stacks) + "  " +
              std::to_string(int(std::ceil(effect.RemainingSec))) + "s");
  }

  if (count < children.size())
  {
    std::vector<std::unique_ptr<UGuiWidget>> smaller;
    smaller.reserve(count);
    for (size_t i = 0; i < count; ++i)
      smaller.push_back(std::move(children[i]));
    children.swap(smaller);
  }
  else
    children.reserve(count);
  for (auto &[index, row] : changed)
  {
    if (index < children.size())
      children[index] = std::move(row);
    else
      children.push_back(std::move(row));
  }
}

void UCharacterSheetScreen::RefreshTooltip(const CharacterPageSnapshot &snap)
{
  if (Widgets.Tooltip)
  {
    Widgets.Tooltip->SetVisible(false);
    InventoryEntryRef hover;
    bool found = false;
    size_t index;
    auto recoveryHover = [&]
    {
      if (!RecoveryMode)
        return false;
      for (size_t i = 0; i < Widgets.BagSlots.size(); ++i)
        if (Widgets.BagSlots[i]->GetBounds().Contains(PointerX, PointerY))
        {
          index = i;
          return true;
        }
      return false;
    };
    if (PickBackpackSlot(PointerX, PointerY, index) || recoveryHover())
    {
      hover = RecoveryMode ? (index < snap.recovery.size()
                                  ? snap.recovery[index]
                                  : InventoryEntryRef{})
                           : snap.slots[index];
      found = !hover.empty;
    }
    auto *session = dynamic_cast<UGameSession *>(Stats);
    auto world = session ? session->GetWorld() : nullptr;
    if (!found && Widgets.DollPanel->IsVisible() && world)
      if (auto *actor = world->GetControlledCreature())
      {
        for (size_t i = 0; i < Widgets.ArmorSlots.size(); ++i)
          if (Widgets.ArmorSlots[i]->GetBounds().Contains(PointerX, PointerY))
          {
            hover = actor->GetInventory().GetEquippedArmor(i);
            found = !hover.empty;
          }
        if (Widgets.ToolSlots[0]->GetBounds().Contains(PointerX, PointerY))
        {
          auto *active = actor->GetInventory().GetActiveEntryRef();
          if (active)
          {
            hover = *active;
            found = !hover.empty;
          }
        }
        if (Widgets.ToolSlots[1]->GetBounds().Contains(PointerX, PointerY))
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
          *Widgets.Tooltip, text, PointerX, PointerY, tooltipArea, Context->GetTheme(),
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
          for (auto *slot : Widgets.BagSlots)
            test(slot);
          test(Widgets.Search);
          test(Widgets.SortButton);
          test(Widgets.DropButton);
        }
        if (section == 1)
        {
          for (auto *slot : Widgets.ArmorSlots)
            test(slot);
          for (auto *slot : Widgets.ToolSlots)
            test(slot);
        }
        if (section == 3)
          for (auto *button : Widgets.AppearanceButtons)
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
      auto bounds = Widgets.ToolSlots[0]->GetBounds();
      GuiMouseEvent event;
      event.X = bounds.X + bounds.W / 2;
      event.Y = bounds.Y + bounds.H / 2;
      Widgets.ToolSlots[0]->OnMouseDown(event);
      event.X += 32;
      Widgets.ToolSlots[0]->OnMouseMove(event);
      activeRowHandDrag = session->IsDragging() &&
                          session->GetDrag().source.bar == 2 &&
                          session->GetDrag().source.slot == 1 &&
                          session->GetDrag().entry.instanceId ==
                              inventory.GetSlot(21).instanceId;
      session->CancelDrag();
      Widgets.ToolSlots[0]->ClearPressState();
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
  if (!Stats || Stats->GetCharacterLookSnapshot().typeId != "human")
    throw std::runtime_error("Appearance UI QA requires controlled human");
  const auto original = Stats->GetCharacterLookSnapshot();
  SetVisible(true);
  if (Hosted)
    SetSection(3);
  else
    Widgets.AppearanceToggle->Activate();
  Widgets.AppearanceButtons[0]->Activate();
  Widgets.AppearanceButtons[1]->Activate();
  Widgets.AppearanceButtons[2]->Activate();
  Widgets.AppearanceButtons[2]->Activate();
  Widgets.AppearanceButtons[3]->Activate();
  const auto desired = DraftAppearance;
  const auto outfit = DraftSkin;
  const bool draftIsolated =
      Stats->GetCharacterLookSnapshot().humanAppearance.Key() ==
          original.humanAppearance.Key() &&
      Stats->GetCharacterLookSnapshot().skinId == original.skinId;
  const bool previewWorks = PreviewTexture != 0;
  Widgets.AppearanceButtons[4]->Activate();
  auto applied = Stats->GetCharacterLookSnapshot();
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
    Widgets.AppearanceToggle->Activate();
  Widgets.AppearanceButtons[2]->Activate();
  Widgets.AppearanceButtons[5]->Activate();
  const bool cancelled =
      Stats->GetCharacterLookSnapshot().humanAppearance.Key() ==
          desired.Key() &&
      Stats->GetCharacterLookSnapshot().skinId == outfit;
  nlohmann::json layouts = nlohmann::json::array();
  if (Hosted)
    SetSection(3);
  else
    Widgets.AppearanceToggle->Activate();
  for (auto size :
       std::array<std::pair<int, int>, 2>{{{1280, 720}, {640, 480}}})
  {
    OnViewportChanged(size.first, size.second);
    bool valid = true;
    for (auto *b : Widgets.AppearanceButtons)
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
  DraftInitialized = false;
  OnViewportChanged(1280, 720);
  RefreshLabels();
  if (Hosted)
    SetSection(3);
  else
    Widgets.AppearanceToggle->Activate();
  Widgets.AppearanceButtons[0]->Activate();
  Widgets.AppearanceButtons[2]->Activate();
  Widgets.AppearanceButtons[2]->Activate();
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

void UCharacterSheetScreen::ResetAppearanceDraft(const CharacterLookSnapshot &snap,
                                                  std::string base)
{
  auto appearance = snap.humanAppearance;
  auto skin = snap.skinId;
  DraftAppearance = std::move(appearance);
  DraftSkin.swap(skin);
  DraftBase.swap(base);
  DraftOwnerId = snap.OwnerId;
  DraftInitialized = true;
}
void UCharacterSheetScreen::RefreshAppearanceControls(bool human)
{
  Widgets.AppearanceToggle->SetVisible(!Hosted && human);
  if (!human && EditingAppearance)
  {
    EditingAppearance = false;
    Relayout();
  }
  Widgets.AppearanceButtons[0]->SetLabel(
      DraftAppearance.bodyVariant == "female" ? "Body: female" : "Body: male");
  Widgets.AppearanceButtons[1]->SetLabel(DraftSkin == "human_guard" ? "Outfit: guard"
                                 : DraftSkin == "human_adventurer"
                                     ? "Outfit: traveler"
                                     : "Outfit: default");
  const std::array<const char *, 6> hair{"No hair", "Short",    "Bob",
                                         "Long",    "Ponytail", "Curly"};
  const std::array<const char *, 5> color{"Black", "Chestnut", "Brown", "Blond",
                                          "Ginger"};
  for (size_t i = 0; i < hair.size(); ++i)
    if (DraftAppearance.hairstyleId == HumanAppearance::Hairstyles[i])
      Widgets.AppearanceButtons[2]->SetLabel(std::string("Hairstyle: ") + hair[i]);
  for (size_t i = 0; i < color.size(); ++i)
    if (DraftAppearance.hairColorId == HumanAppearance::Colors[i])
      Widgets.AppearanceButtons[3]->SetLabel(std::string("Hair color: ") + color[i]);
  Widgets.AppearanceButtons[4]->SetLabel("Apply");
  Widgets.AppearanceButtons[5]->SetLabel("Cancel");
}

void UCharacterSheetScreen::RenderCharacterPreview(const CharacterLookSnapshot &snap)
{
  if (!Widgets.PreviewViewport)
  {
    return;
  }
  if (!CreaturePreview || !snap.valid || snap.typeId.empty())
  {
    if (PreviewTexture != 0)
    {
      glDeleteTextures(1, &PreviewTexture);
      PreviewTexture = 0;
    }
    Widgets.PreviewViewport->SetPreviewTexture(0);
    return;
  }

  const GuiRect b = Widgets.PreviewViewport->GetBounds();
  const int size = std::max(64, std::min(b.W, b.H));

  std::array<WornArmorPreviewSlot, kWornArmorSlotCount> armorSlots{};
  std::string armorKey;
  std::string mainId;
  std::string offId;
  std::string wieldKey;
  ItemVisualInputs mainInputs, offInputs;
  for (size_t i = 0; i < kWornArmorSlotCount; ++i)
  {
    const auto &s = snap.equippedArmor[i];
    if (!s.itemId.empty() && !s.broken)
      armorSlots[i].ItemId = s.itemId;
    armorKey += s.itemId;
    armorKey.push_back('|');
    armorKey += s.broken ? '1' : '0';
    armorKey.push_back(';');
  }
  const auto &main = snap.equippedTools[0];
  const auto &off = snap.equippedTools[1];
  if (!main.itemId.empty() && !main.broken && !main.isBlock)
    mainId = main.itemId;
  if (!off.itemId.empty() && !off.broken && !off.isBlock)
    offId = off.itemId;
  wieldKey = mainId + "|" + offId;

  if (auto *session = dynamic_cast<UGameSession *>(Stats))
    if (auto world = session->GetWorld())
      if (const auto *actor = world->GetControlledCreature())
      {
        const auto &inv = actor->GetInventory();
        mainInputs = MakeItemVisualInputs(inv.GetActiveEntryRef());
        offInputs = MakeItemVisualInputs(&inv.GetEquippedOffhand());
        wieldKey += "|fill=" + std::to_string(mainInputs.ContentsUnits) + ":" +
                    std::to_string(mainInputs.HasWater) + ":" +
                    std::to_string(offInputs.ContentsUnits) + ":" +
                    std::to_string(offInputs.HasWater);
      }
  wieldKey +=
      "|revision=" + std::to_string(UItemDefinitionStorage::ContentRevision());
  const float yaw = Widgets.PreviewViewport->GetYaw();
  const float pitch = Widgets.PreviewViewport->GetPitch();
  const auto &look = EditingAppearance ? DraftAppearance : snap.humanAppearance;
  const auto &skinId = EditingAppearance ? DraftSkin : snap.skinId;
  auto appearanceKey = look.Key();
  const bool sameCache =
      appearanceKey == LastCachedAppearance && PreviewTexture != 0 &&
      size == LastPreviewSize && snap.typeId == LastCachedTypeId &&
      skinId == LastCachedSkinId && armorKey == LastCachedArmorKey &&
      wieldKey == LastCachedWieldKey &&
      std::abs(yaw - LastPreviewYaw) < 0.01f &&
      std::abs(pitch - LastPreviewPitch) < 0.01f;
  if (sameCache)
  {
    return;
  }

  auto typeKey = snap.typeId;
  auto skinKey = skinId;
  UGlTextureOwner texture(CreaturePreview->RenderToUniqueTexture(
      typeKey, skinKey, size, yaw, pitch, 0.f, false, &armorSlots,
      mainId.empty() ? nullptr : &mainId, offId.empty() ? nullptr : &offId,
      &look, &mainInputs, &offInputs));
  if (!texture.Get())
    return;

  UGlTextureOwner retired(PreviewTexture);
  PreviewTexture = texture.Release();
  Widgets.PreviewViewport->SetPreviewTexture(PreviewTexture);
  LastPreviewYaw = yaw;
  LastPreviewPitch = pitch;
  LastPreviewSize = size;
  LastCachedTypeId.swap(typeKey);
  LastCachedSkinId.swap(skinKey);
  LastCachedAppearance.swap(appearanceKey);
  LastCachedArmorKey.swap(armorKey);
  LastCachedWieldKey.swap(wieldKey);
}

void UCharacterSheetScreen::Update(double /*dt*/)
{
  if (Visible)
  {
    try
    {
      if (Icons && Icons->PendingItemIcons())
        Icons->WarmupItemIcons(3);
    }
    catch (const std::bad_alloc &) { /* The icon queue retains the current request. */ }
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
  return (Widgets.Search ? Widgets.Search->GetText() : "") + "|" +
         std::to_string(RecoveryMode) + "|" + std::to_string(RecoveryOffset) +
         "|" + std::to_string(Widgets.StatsScroll->GetScrollY()) + "|" +
         DraftAppearance.Key() + "|" + DraftSkin + "|" +
         std::to_string(Widgets.PreviewViewport->GetYaw()) + "|" +
         std::to_string(Widgets.PreviewViewport->GetPitch());
}
GuiRect UCharacterSheetScreen::GetSearchBounds() const
{
  return Widgets.Search->GetBounds();
}
GuiRect UCharacterSheetScreen::GetRecoveryButtonBounds() const
{
  return Widgets.RecoveryButton->GetBounds();
}
GuiRect UCharacterSheetScreen::GetBackpackSlotBounds(size_t index) const
{
  return Root && index < Widgets.BagSlots.size() ? Widgets.BagSlots[index]->GetBounds() : GuiRect{};
}
GLuint UCharacterSheetScreen::GetBackpackIconTexture(size_t index) const
{
  return Root && index < Widgets.BagSlots.size() ? Widgets.BagSlots[index]->GetIconTexture() : 0;
}
} // namespace cutum
