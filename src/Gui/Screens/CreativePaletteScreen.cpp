#include "Gui/Screens/CreativePaletteScreen.h"
#include "Content/ContentTypeRegistry.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include "Game/ModePolicy.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUContentCatalog.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Layout/DockedOverlayLayout.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Layout/GuiTooltipLayout.h"
#include "Gui/Layout/PlayerWindowLayout.h"
#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Items/ItemDefinitionStorage.h"
#include "World/Core/World.h"

#include "ResourcePacks/BlockNameUtil.h"

#include <algorithm>
#include <new>
#include <typeinfo>

namespace cutum
{

namespace
{
constexpr size_t kGridChunkSize = 256;
constexpr size_t kBackgroundIconChecks = 64;
}

class UCreativePaletteScreen::GridSlot final : public UGuiSlot
{
public:
  using UGuiSlot::UGuiSlot;
  std::string SpawnHint;
};

struct UCreativePaletteScreen::GridClickAction
{
  UCreativePaletteScreen *owner;
  size_t index;
  void operator()() const { owner->ClickGridSlot(index); }
};

struct UCreativePaletteScreen::GridDragAction
{
  UCreativePaletteScreen *owner;
  size_t index;
  void operator()() const { owner->DragGridSlot(index); }
};

UCreativePaletteScreen::UCreativePaletteScreen(
    IUContentCatalog *catalog, UGameSession *session, IUGuiIconSource *icons,
    UContentPreviewRenderer *previewRenderer)
    : Catalog(catalog), Session(session), Icons(icons),
      PreviewRenderer(previewRenderer)
{
}

UCreativePaletteScreen::~UCreativePaletteScreen() = default;

const UCreativePaletteScreen::GridWidgets &UCreativePaletteScreen::GridChildren() const
{
  static const GridWidgets empty;
  return Scroll ? Scroll->Content().GetChildren() : empty;
}

UCreativePaletteScreen::GridSlot &UCreativePaletteScreen::GridSlotAt(size_t index) const
{
  return static_cast<GridSlot &>(*GridChildren()[index]);
}

size_t UCreativePaletteScreen::GetGridEntryCount() const
{
  return GridChildren().size();
}

bool UCreativePaletteScreen::PickHotbarStrip(int x, int y,
                                             SlotAddress &out) const
{
  if (!Visible)
  {
    return false;
  }
  for (size_t i = 0; i < HotbarStripSlots.size(); ++i)
  {
    const UGuiSlot *slot = HotbarStripSlots[i];
    if (!slot || !slot->IsVisible() || !slot->GetBounds().Contains(x, y))
    {
      continue;
    }
    out = SlotAddress{};
    out.surface = SlotSurface::Hotbar;
    out.bar = 0;
    out.slot = i;
    return true;
  }
  return false;
}

bool UCreativePaletteScreen::PickGridSlot(int x, int y, SlotAddress &out) const
{
  if (!Visible || !Scroll || !Scroll->GetBounds().Contains(x, y))
  {
    return false;
  }
  const auto [first, end] = VisibleGridRange();
  for (size_t i = first; i < end; ++i)
  {
    const auto &slot = GridSlotAt(i);
    if (!slot.IsVisible() || !slot.GetBounds().Contains(x, y))
    {
      continue;
    }
    out = SlotAddress{};
    out.surface = SlotSurface::PaletteGrid;
    out.paletteKind = GridKind;
    out.entryId = GridViews[i].ref.Id;
    out.bar = 0;
    out.slot = 0;
    return true;
  }
  return false;
}

bool UCreativePaletteScreen::PickSlot(int x, int y, SlotAddress &out) const
{
  if (PickHotbarStrip(x, y, out))
  {
    return true;
  }
  return PickGridSlot(x, y, out);
}

void UCreativePaletteScreen::SetVisible(bool visible)
{
  if (visible)
  {
    RefreshListingPolicy();
  }
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (!visible && !Hosted)
  {
    SelectedEntryId.clear();
    if (PreviewDock)
    {
      PreviewDock->ClearSelection();
    }
  }
  else
  {
    RelayoutPanel();
  }
}

void UCreativePaletteScreen::SetCatalogAllowed(bool allowed)
{
  CatalogAllowed = allowed;
  if (!CatalogAllowed)
  {
    Listing = PaletteListing::Backpack;
    Built = false;
  }
  if (CatalogButton)
  {
    CatalogButton->SetVisible(CatalogAllowed);
  }
}

void UCreativePaletteScreen::SetListingBackpack(bool backpack)
{
  if (backpack)
  {
    Listing = PaletteListing::Backpack;
  }
  else if (CatalogAllowed)
  {
    Listing = PaletteListing::Catalog;
  }
  Built = false;
  RelayoutPanel();
}

void UCreativePaletteScreen::RefreshListingPolicy()
{
  const bool creative =
      Session && Session->GetInventoryMode() == InventoryMode::Creative;
  SetCatalogAllowed(creative);
}

InventoryMode UCreativePaletteScreen::CurrentListingMode() const
{
  return Listing == PaletteListing::Backpack ? InventoryMode::Owned
                                             : InventoryMode::Creative;
}

void UCreativePaletteScreen::Toggle() { SetVisible(!Visible); }

void UCreativePaletteScreen::ApplyMainTab(int tab)
{
  switch (tab)
  {
  case 0:
    Kind = ContentKind::Block;
    break;
  case 1:
    Kind = ContentKind::Object;
    break;
  case 2:
    Kind = ContentKind::UCreature;
    break;
  case 3:
    Kind = ContentKind::Skin;
    break;
  default:
    Kind = ContentKind::Item;
    break;
  }
  if (MainTabs)
  {
    MainTabs->SetActiveTab(tab);
  }
  ActiveTypeId.clear();
  SelectedEntryId.clear();
  TypeTabsCacheValid = false;
  if (PreviewDock)
  {
    PreviewDock->ClearSelection();
  }
  Built = false;
  RelayoutPanel();
}

void UCreativePaletteScreen::OpenWithMainTab(int tab)
{
  SetListingBackpack(false);
  ApplyMainTab(tab);
  SetVisible(true);
}

int UCreativePaletteScreen::GetActiveMainTab() const
{
  if (MainTabs)
  {
    return MainTabs->GetActiveTab();
  }
  switch (Kind)
  {
  case ContentKind::Block:
    return 0;
  case ContentKind::Object:
    return 1;
  case ContentKind::UCreature:
    return 2;
  case ContentKind::Skin:
    return 3;
  case ContentKind::Item:
  default:
    return 4;
  }
}

void UCreativePaletteScreen::Build(UGuiContext &ctx)
{
  const auto &theme = ctx.GetTheme();
  auto root = std::make_unique<UGuiPanel>(&theme);
  root->SetDrawBackground(false);
  root->SetClipChildren(false);
  root->SetVisible(Visible);
  root->ReserveChildren(Session ? 17 : 6);

  auto panel = std::make_unique<UGuiPanel>(&theme);
  auto *nextPanel = panel.get();
  panel->SetDrawBackground(true);
  panel->SetZOrder(10);
  panel->ReserveChildren(5);

  auto mainTabs = std::make_unique<UGuiTabBar>(&theme);
  auto *nextMainTabs = mainTabs.get();
  mainTabs->SetTabs({"Blocks", "Objects", "Creatures", "Skins", "Items"});
  mainTabs->SetActiveTab(GetActiveMainTab());
  mainTabs->SetOnTabChanged([this](int tab) { ApplyMainTab(tab); });

  auto tooltip = std::make_unique<UGuiLabel>(&theme, "");
  auto *nextTooltip = tooltip.get();
  tooltip->SetVisible(false);
  panel->AddChild(std::move(tooltip));

  auto usageHint = std::make_unique<UGuiLabel>(
      &theme,
      "Catalog templates | Picked items: Backpack | Click: assign | 1-0: hand");
  auto *nextUsageHint = usageHint.get();
  usageHint->SetUseSecondaryColor(true);
  usageHint->SetVisible(false);
  panel->AddChild(std::move(usageHint));

  auto stripLabel = std::make_unique<UGuiLabel>(&theme, "Hotbar (drop here)");
  auto *nextStripLabel = stripLabel.get();
  stripLabel->SetUseSecondaryColor(true);
  stripLabel->SetZOrder(49);
  root->AddChild(std::move(stripLabel));

  auto catalogBtn = std::make_unique<UGuiButton>(&theme, "Catalog");
  auto *nextCatalogButton = catalogBtn.get();
  catalogBtn->SetOnClick([this] { SetListingBackpack(false); });
  root->AddChild(std::move(catalogBtn));

  auto bagBtn = std::make_unique<UGuiButton>(&theme, "Backpack");
  auto *nextBackpackButton = bagBtn.get();
  bagBtn->SetOnClick([this]
  {
    if (Session)
      Session->OpenPlayerWindow(0);
  });
  root->AddChild(std::move(bagBtn));

  panel->AddChild(std::move(mainTabs));
  root->AddChild(std::move(panel));
  auto &tabChild = nextPanel->Children.emplace_back();
  auto &scrollChild = nextPanel->Children.emplace_back();

  auto navigation = std::make_unique<UGuiTabBar>(&theme);
  auto *nextPlayerTabs = navigation.get();
  navigation->SetTabs({"Backpack", "Equipment", "Status", "Appearance", "Catalog"});
  navigation->SetActiveTab(4);
  navigation->SetOnTabChanged([this](int section)
  {
    if (section != 4 && Session)
      Session->OpenPlayerWindow(section);
  });
  root->AddChild(std::move(navigation));
  auto &dockChild = root->Children.emplace_back();

  std::array<UGuiSlot *, 10> nextStripSlots{};
  auto *nextStripDivider = Session ? BuildHotbarStrip(*root, theme, nextStripSlots) : nullptr;

  if (Root)
  {
    // Keep the published widgets and their live interaction state.
    const auto transfer = [](UGuiWidget &owner, const UGuiWidget *widget,
                             std::unique_ptr<UGuiWidget> &target)
    {
      const auto position = std::find_if(owner.Children.begin(), owner.Children.end(),
          [widget](const auto &child) { return child.get() == widget; });
      target = std::move(*position);
    };
    transfer(*Panel, SubTabs, tabChild);
    transfer(*Panel, Scroll, scrollChild);
    transfer(*RootShell, PreviewDock->GetPanel(), dockChild);
  }
  else
  {
    auto subTabs = std::make_unique<UGuiTabBar>(&theme);
    subTabs->SetOnTabChanged([this](int tab) { SelectTypeTab(tab); });
    auto scroll = std::make_unique<UGuiScrollView>(&theme);
    scroll->SetAfterScrollLayout([this](UGuiScrollView &) { LayoutGridInScroll(); });
    auto dock = std::make_unique<UContentPreviewDock>(&theme, PreviewRenderer);
    const auto types = Catalog ? Catalog->GetTypeIds(Kind) : std::vector<std::string>{};
    std::string initialType = types.empty() ? "misc" : types.front();
    SubTabs = subTabs.get();
    Scroll = scroll.get();
    tabChild = std::move(subTabs);
    scrollChild = std::move(scroll);
    dockChild = dock->ReleasePanel();
    PreviewDock = std::move(dock);
    ActiveTypeId.swap(initialType);
  }

  Theme = &theme;
  Renderer = &ctx.GetRenderer();
  RootShell = root.get();
  Panel = nextPanel;
  MainTabs = nextMainTabs;
  PlayerTabs = nextPlayerTabs;
  TooltipLabel = nextTooltip;
  UsageHintLabel = nextUsageHint;
  CatalogButton = nextCatalogButton;
  BackpackButton = nextBackpackButton;
  HotbarStripLabel = nextStripLabel;
  HotbarStripDivider = nextStripDivider;
  HotbarStripSlots = nextStripSlots;
  Root = std::move(root);
  RelayoutPanel();
}

void UCreativePaletteScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  RelayoutPanel();
}

void UCreativePaletteScreen::RelayoutPanel()
{
  if (!Panel || !Theme)
  {
    return;
  }
  GuiRect panelRect;
  if (Hosted)
  {
    const auto shared =
        PlayerWindowLayout::Compute(ViewportW, ViewportH, Metrics,
                                    GetContentOffsetX(), GetContentOffsetY());
    RootShell->SetBounds(
        {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});
    PlayerTabs->SetVisible(false);
    CatalogButton->SetVisible(false);
    BackpackButton->SetVisible(false);
    HotbarStripLabel->SetVisible(false);
    if (HotbarStripDivider)
      HotbarStripDivider->SetVisible(false);
    if (HotbarStripDivider)
      for (auto *slot : HotbarStripSlots)
        slot->SetVisible(false);
    panelRect = shared.Content;
    Panel->SetBounds(panelRect);
    if (PreviewDock)
      PreviewDock->Relayout(shared.Preview);
  }
  else
  {
    DockedLayout layout =
        DockedOverlayLayout::Compute(ViewportW, ViewportH, GetContentOffsetX(),
                                     GetContentOffsetY(), 70, 28, *Theme);
    const int slotSize = Theme->HotbarSlotSize;
    const int hotbarReserve =
        slotSize + Theme->HotbarMarginBottom + Theme->Padding * 2;
    const int maxPanelH =
        std::max(Scaled(120), ViewportH - hotbarReserve - ViewportH / 12);
    if (layout.main.H > maxPanelH)
    {
      const int shrink = layout.main.H - maxPanelH;
      layout.main.H = maxPanelH;
      layout.main.Y += shrink / 2;
      layout.preview.H = maxPanelH;
      layout.preview.Y = layout.main.Y;
    }
    const int maxBottom = GetContentOffsetY() + ViewportH - hotbarReserve;
    if (layout.main.Y + layout.main.H > maxBottom)
    {
      layout.main.Y = maxBottom - layout.main.H;
      layout.preview.Y = layout.main.Y;
    }
    if (RootShell)
    {
      RootShell->SetBounds(
          {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});
    }
    const int navH = Theme->TabBarHeight + Theme->Padding;
    if (PlayerTabs)
    {
      PlayerTabs->SetBounds(
          {layout.main.X, layout.main.Y, layout.main.W, Theme->TabBarHeight});
      PlayerTabs->SetVisible(true);
      PlayerTabs->SetActiveTab(4);
    }
    layout.main.Y += navH;
    layout.main.H = std::max(120, layout.main.H - navH);
    Panel->SetBounds(layout.main);
    const int modeW = 100;
    panelRect = layout.main;
    panelRect.X += modeW;
    panelRect.W = std::max(160, panelRect.W - modeW);
    Panel->SetBounds(panelRect);
    if (CatalogButton)
    {
      CatalogButton->SetVisible(CatalogAllowed);
      CatalogButton->SetBounds(
          {panelRect.X - modeW, panelRect.Y, modeW - 6, 40});
    }
    if (BackpackButton)
    {
      BackpackButton->SetVisible(true);
      BackpackButton->SetBounds(
          {panelRect.X - modeW, panelRect.Y + 46, modeW - 6, 40});
    }
    if (PreviewDock)
    {
      PreviewDock->Relayout(layout.preview);
    }
  }
  const int panelX = panelRect.X;
  const int panelY = panelRect.Y;
  const int panelW = panelRect.W;
  const int panelH = panelRect.H;

  const int pad = Theme->Padding;
  const int tabH = Theme->TabBarHeight;
  if (MainTabs)
  {
    MainTabs->SetBounds({panelX + pad, panelY + pad, panelW - pad * 2, tabH});
  }
  if (SubTabs)
  {
    SubTabs->SetBounds(
        {panelX + pad, panelY + pad + tabH + pad, panelW - pad * 2, tabH});
  }
  const bool showHint = panelW >= Scaled(650);
  const int hintH = showHint ? Theme->FontSizeBody + 4 : 0;
  if (UsageHintLabel)
  {
    UsageHintLabel->SetVisible(showHint);
    UsageHintLabel->SetBounds({panelX + pad,
                               panelY + pad + tabH + pad + tabH + pad / 2,
                               panelW - pad * 2, hintH});
  }
  if (Scroll)
  {
    const int hintGap = hintH + pad / 2;
    const int scrollTop = panelY + pad + tabH + pad + tabH + pad + hintGap;
    const int scrollH = std::max(0, panelH - (scrollTop - panelY) - pad);
    Scroll->SetBounds({panelX + pad, scrollTop, panelW - pad * 2, scrollH});
    if (Built)
    {
      Scroll->LayoutContent();
    }
  }
  LayoutHotbarStrip();
}

UGuiPanel *UCreativePaletteScreen::BuildHotbarStrip(
    UGuiPanel &root, const GuiTheme &theme, std::array<UGuiSlot *, 10> &slots)
{
  for (size_t i = 0; i < slots.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(&theme);
    slot->SetOnClick([this, i]
    {
      if (!Session->ApplyPendingAssignment(0, i))
        Session->SelectSlot(0, i);
    });
    slot->SetOnBeginDrag([this, i]
    {
      const auto entry = Session->GetHotbarEntryRef(0, i);
      if (!entry.empty)
      {
        SlotAddress address;
        address.surface = SlotSurface::Hotbar;
        address.slot = i;
        Session->BeginDragFromSlot(address, entry);
      }
    });
    slot->SetCornerHint(std::to_string((i + 1) % 10));
    slot->SetZOrder(50);
    slots[i] = static_cast<UGuiSlot *>(root.AddChild(std::move(slot)));
  }
  auto divider = std::make_unique<UGuiPanel>(&theme);
  divider->SetDrawBackground(true);
  divider->SetZOrder(49);
  return static_cast<UGuiPanel *>(root.AddChild(std::move(divider)));
}

void UCreativePaletteScreen::LayoutHotbarStrip()
{
  if (Hosted || !HotbarStripDivider || !Theme)
    return;
  HotbarStripLabel->SetVisible(true);
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int sectionGap = gap * 3 + std::max(2, Theme->BorderThickness * 2);
  const int slotCount = static_cast<int>(HotbarStripSlots.size());
  const int totalW = slotCount * slotSize + std::max(0, slotCount - 1) * gap +
                     (slotCount > 5 ? sectionGap - gap : 0);
  const int startX = GetContentOffsetX() + (ViewportW - totalW) / 2;
  const int rowY =
      GetContentOffsetY() + ViewportH - Theme->HotbarMarginBottom - slotSize;
  if (HotbarStripLabel)
  {
    HotbarStripLabel->SetBounds(
        {startX, rowY - Theme->FontSizeBody - Theme->Padding / 2, totalW,
         Theme->FontSizeBody + 4});
  }
  int x = startX;
  for (size_t i = 0; i < HotbarStripSlots.size(); ++i)
  {
    UGuiSlot *slot = HotbarStripSlots[i];
    if (slot)
    {
      slot->SetBounds({x, rowY, slotSize, slotSize});
      slot->SetVisible(true);
      x += slotSize;
      if (i + 1 < HotbarStripSlots.size())
      {
        x += (i == 4) ? sectionGap : gap;
      }
    }
  }
  if (HotbarStripDivider && HotbarStripSlots.size() > 5 &&
      HotbarStripSlots[4] && HotbarStripSlots[5])
  {
    const GuiRect left = HotbarStripSlots[4]->GetBounds();
    const GuiRect right = HotbarStripSlots[5]->GetBounds();
    const int mid = (left.X + left.W + right.X) / 2;
    const int divW = std::max(2, Theme->BorderThickness * 2);
    const int divH = slotSize * 3 / 4;
    HotbarStripDivider->SetVisible(true);
    HotbarStripDivider->SetBounds(
        {mid - divW / 2, rowY + (slotSize - divH) / 2, divW, divH});
  }
  else if (HotbarStripDivider)
  {
    HotbarStripDivider->SetVisible(false);
  }
}

void UCreativePaletteScreen::SyncHotbarStrip()
{
  if (Hosted || !HotbarStripDivider)
    return;

  auto primary = Session->GetBarSlots(0);
  std::array<GLuint, 10> textures{};
  if (Icons)
    for (size_t i = 0; i < primary.size(); ++i)
      textures[i] = Icons->GetEntryIcon(primary[i].entryKind, primary[i].Id);
  for (size_t i = 0; i < HotbarStripSlots.size() && i < primary.size(); ++i)
  {
    UGuiSlot *slot = HotbarStripSlots[i];
    if (!slot)
    {
      continue;
    }
    slot->SetSelected(primary[i].selected);
    slot->Label.swap(primary[i].label);
    slot->CountHint.swap(primary[i].CountLabel);
    slot->SetIconTexture(textures[i]);
    slot->SetWearProgress(primary[i].entryKind == InventoryEntryKind::Item
                              ? primary[i].wear
                              : 0.f);
    slot->SetBroken(primary[i].entryKind == InventoryEntryKind::Item &&
                    primary[i].broken);
  }
}

void UCreativePaletteScreen::SetPointerPosition(int x, int y)
{
  PointerX = x;
  PointerY = y;
}

void UCreativePaletteScreen::SetPointerPressed(bool pressed)
{
  if (!pressed)
  {
    HoldTimer = 0.0;
    HoldSlotIndex = -1;
  }
  PointerPressed = pressed;
}

void UCreativePaletteScreen::UpdateTooltip()
{
  if (!TooltipLabel || !Theme)
  {
    return;
  }
  std::string text;
  int tipX = PointerX;
  int tipY = PointerY;
  if (SubTabs && SubTabs->GetBounds().Contains(PointerX, PointerY) &&
      !SubTabs->GetTabLabels().empty())
  {
    const auto bounds = SubTabs->GetBounds();
    const auto &labels = SubTabs->GetTabLabels();
    const int count = static_cast<int>(labels.size());
    const int index = std::min(count - 1,
        (PointerX - bounds.X) / std::max(1, bounds.W / count));
    text = labels[static_cast<size_t>(index)];
  }
  else
  {
    size_t index = GetGridEntryCount();
    if (HoldTimer >= kHoldTooltipSeconds && HoldSlotIndex >= 0)
      index = static_cast<size_t>(HoldSlotIndex);
    else if (Scroll && Scroll->GetBounds().Contains(PointerX, PointerY))
    {
      const auto [first, end] = VisibleGridRange();
      for (size_t i = first; i < end; ++i)
        if (GridSlotAt(i).GetBounds().Contains(PointerX, PointerY))
        {
          index = i;
          break;
        }
    }
    if (index < GetGridEntryCount())
    {
      const auto &id = GridViews[index].ref.Id;
      const auto &slot = GridSlotAt(index);
      text = slot.GetLabel().empty() ? HumanizeBlockName(id) : slot.GetLabel();
      if (!slot.SpawnHint.empty())
        text += "\n" + slot.SpawnHint;
      if (GridKind == ContentKind::Item && Session)
      {
        const auto world = Session->GetWorld();
        const auto *items = world ? world->GetItemDefinitionStorage() : nullptr;
        text = BuildItemTooltipText(text, items ? items->Get(id) : nullptr);
      }
      if (tipX < 0 || tipY < 0)
      {
        const auto bounds = slot.GetBounds();
        tipX = bounds.X + bounds.W / 2;
        tipY = bounds.Y + bounds.H / 2;
      }
    }
  }

  if (text.empty() || tipX < 0 || tipY < 0)
  {
    TooltipLabel->SetVisible(false);
    return;
  }

  GuiRect viewport{GetContentOffsetX(), GetContentOffsetY(), ViewportW,
                   ViewportH};
  if (Hosted)
  {
    viewport =
        PlayerWindowLayout::Compute(ViewportW, ViewportH, Metrics,
                                    GetContentOffsetX(), GetContentOffsetY())
            .Window;
    viewport.Y += Theme->TitleBarHeight;
    viewport.H -= Theme->TitleBarHeight;
  }
  const int textW = MeasureTooltipTextWidth(text, *Theme, Renderer);
  LayoutTooltipNearPointer(*TooltipLabel, text, tipX, tipY, viewport, *Theme,
                           textW);
}

void UCreativePaletteScreen::Update(double dt) try
{
  if (!Visible || !Panel || !Catalog || !Theme)
  {
    return;
  }
  RelayoutPanel();
  SyncHotbarStrip();

  SyncTypeTabs();
  if (!Built && !PointerPressed)
    RebuildGrid();
  else if (Built && !IsGridComplete())
    AppendGridSlotsChunk();

  if (IsGridComplete() && GridPreviewPending)
  {
    SyncPreviewDock();
    GridPreviewPending = false;
  }
  if (Icons && Built)
  {
    RegisterGridIcons();
    const size_t visibleWork = WarmVisibleGridIcons();
    if (GridKind == ContentKind::Block)
      Icons->WarmupBlockIcons(4 - visibleWork);
    if (GridKind == ContentKind::Item)
      Icons->WarmupItemIcons(8);
    RefreshGridIconTextures();
  }

  if (PointerPressed)
  {
    int slotUnderPointer = -1;
    if (Scroll && Scroll->GetBounds().Contains(PointerX, PointerY))
    {
      const auto [first, end] = VisibleGridRange();
      for (size_t i = first; i < end; ++i)
        if (GridSlotAt(i).GetBounds().Contains(PointerX, PointerY))
        {
          slotUnderPointer = static_cast<int>(i);
          break;
        }
    }
    if (slotUnderPointer < 0 || slotUnderPointer != HoldSlotIndex)
      HoldTimer = 0.0;
    else
      HoldTimer += dt;
    HoldSlotIndex = slotUnderPointer;
  }

  if (PreviewDock)
  {
    PreviewDock->Update(dt);
  }
  UpdateTooltip();
}
catch (const std::bad_alloc &)
{
  // Each publication retains enough state to retry on the next update.
}

void UCreativePaletteScreen::SyncTypeTabs()
{
  if (!SubTabs || !Catalog || !Session)
    return;
  const InventoryMode mode = CurrentListingMode();
  const size_t signature = mode == InventoryMode::Owned ? Session->GetInventorySignature() : 0;
  if (TypeTabsCacheValid && TypeTabSignature == signature &&
      TypeTabKind == Kind && TypeTabMode == mode)
    return;

  auto types = Catalog->GetTypeIds(Kind);
  std::vector<std::string> labels;
  std::vector<bool> enabled;
  labels.reserve(types.size());
  enabled.reserve(types.size());
  for (const std::string &id : types)
  {
    labels.push_back(Catalog->GetTypeDisplayName(id));
    enabled.push_back(Session->HasEntries(Kind, id, mode));
  }
  if (labels.empty())
  {
    labels.push_back("Misc");
    enabled.push_back(true);
  }

  const auto current = std::find(types.begin(), types.end(), ActiveTypeId);
  const int previous = current == types.end() ? 0 : static_cast<int>(current - types.begin());
  const int active = NearestEnabledIndex(previous, enabled);
  std::string nextType = active >= 0 && static_cast<size_t>(active) < types.size() &&
                                 enabled[static_cast<size_t>(active)]
                            ? types[static_cast<size_t>(active)] : std::string{};
  const bool changed = ActiveTypeId != nextType ||
      (mode == InventoryMode::Owned && TypeTabsCacheValid && signature != TypeTabSignature);

  SubTabs->SetTabs(std::move(labels), std::move(enabled));
  SubTabs->SetActiveTab(std::max(0, active));
  TypeTabTypes.swap(types);
  ActiveTypeId.swap(nextType);
  TypeTabSignature = signature;
  TypeTabKind = Kind;
  TypeTabMode = mode;
  TypeTabsCacheValid = true;
  if (changed)
    Built = false;
}

void UCreativePaletteScreen::SelectTypeTab(int tab)
{
  const auto index = static_cast<size_t>(tab);
  if (index >= TypeTabTypes.size())
    return;
  try
  {
    auto next = TypeTabTypes[index];
    PreviewDock->ClearSelection();
    ActiveTypeId.swap(next);
    SelectedEntryId.clear();
    Built = false;
  }
  catch (const std::bad_alloc &)
  {
    const auto previous = std::find(TypeTabTypes.begin(), TypeTabTypes.end(), ActiveTypeId);
    SubTabs->SetActiveTab(previous == TypeTabTypes.end() ? 0 :
                         static_cast<int>(previous - TypeTabTypes.begin()));
  }
}

void UCreativePaletteScreen::SyncPreviewDock()
{
  if (!PreviewDock)
  {
    return;
  }
  if (SelectedEntryId.empty() || !PreviewRenderer ||
      !PreviewRenderer->SupportsKind(GridKind))
  {
    PreviewDock->ClearSelection();
    return;
  }
  const std::string *label = &SelectedEntryId;
  for (size_t i = 0; i < GetGridEntryCount(); ++i)
    if (GridViews[i].ref.Id == SelectedEntryId)
    {
      label = &GridSlotAt(i).GetLabel();
      break;
    }
  PreviewDock->SetSelection(GridKind, SelectedEntryId, *label);
}

void UCreativePaletteScreen::RenderPreview()
{
  if (PreviewDock)
  {
    PreviewDock->RenderIfDirty();
  }
}

void UCreativePaletteScreen::RebuildGrid()
{
  if (!Scroll || !Catalog || !Session || !Theme)
    return;

  const std::string groupId = ActiveTypeId.empty() ? "misc" : ActiveTypeId;
  auto views = Session->GetEntries(Kind, groupId, CurrentListingMode());
  auto widgets = BuildGridChunk(views, 0, Kind);
  GridViews.swap(views);
  Scroll->Content().Children.swap(widgets);
  GridKind = Kind;
  Built = true;
  GridPreviewPending = true;
  GridRegisteredIcons = GridIconCursor = GridCacheCursor = 0;
  HoldSlotIndex = -1;
  HoldTimer = 0.0;
  ReleaseMaterializedLabels(0);
  Scroll->LayoutContent();
}

void UCreativePaletteScreen::AppendGridSlotsChunk()
{
  auto &content = Scroll->Content();
  const auto start = GetGridEntryCount();
  auto widgets = BuildGridChunk(GridViews, start, GridKind);
  const auto target = start + widgets.size();
  const auto capacity = GridChildren().capacity();
  if (target > capacity)
    content.ReserveChildren(std::max(target, std::min(GridViews.size(), capacity + capacity / 2)));
  for (auto &widget : widgets)
    content.AddChild(std::move(widget));
  ReleaseMaterializedLabels(start);
  Scroll->LayoutContent();
}

UCreativePaletteScreen::GridWidgets UCreativePaletteScreen::BuildGridChunk(
    const std::vector<InventoryEntryView> &views, size_t start, ContentKind kind)
{
  const auto count = std::min(kGridChunkSize, views.size() - start);
  GridWidgets widgets;
  widgets.reserve(count);
  for (size_t i = start; i < start + count; ++i)
  {
    const auto &view = views[i];
    const auto &entry = view.ref;
    auto slot = std::make_unique<GridSlot>(Theme);
    slot->SetLabel(view.label);
    slot->SetSelected(entry.Id == SelectedEntryId);
    if (!entry.IsCatalogReference() && entry.count > 0)
      slot->SetCornerHint(std::to_string(entry.count));
    slot->SetWearProgress(entry.wear);
    slot->SetBroken(entry.broken);
    slot->SetOnClick(GridClickAction{this, i});
    slot->SetOnBeginDrag(GridDragAction{this, i});
    if (kind == ContentKind::UCreature && !Session->CanSpawnCreatureByView(entry.Id))
    {
      slot->SetDimmed(true);
      slot->SpawnHint = Session->GetCreatureSpawnBlockedHint(entry.Id);
    }
    widgets.emplace_back(std::move(slot));
  }
  return widgets;
}

void UCreativePaletteScreen::ReleaseMaterializedLabels(size_t start) noexcept
{
  for (size_t i = start; i < GetGridEntryCount(); ++i)
    std::string{}.swap(GridViews[i].label);
}

void UCreativePaletteScreen::ClickGridSlot(size_t index)
{
  const auto &entry = GridViews[index].ref;
  try
  {
    SelectedEntryId = entry.Id;
  }
  catch (const std::bad_alloc &)
  {
    return;
  }
  GridPreviewPending = true;
  try
  {
    SyncPreviewDock();
    GridPreviewPending = false;
  }
  catch (const std::bad_alloc &) { /* Retry the preview without repeating the slot action. */ }
  const size_t bar = Hosted ? Session->GetActiveHotbarRow() : 0;
  const size_t selected = Session->GetSelectedSlot(bar);
  if (Session->AssignToHotbar(entry, bar, selected))
  {
    Session->ClearPendingAssignment();
    Session->SelectSlot(bar, selected);
  }
  else
  {
    Session->BeginPendingAssignment(entry);
  }
  for (size_t i = 0; i < GetGridEntryCount(); ++i)
    GridSlotAt(i).SetSelected(GridViews[i].ref.Id == SelectedEntryId);
  if (CurrentListingMode() == InventoryMode::Owned)
    Built = false;
}

void UCreativePaletteScreen::DragGridSlot(size_t index)
{
  const auto &entry = GridViews[index].ref;
  SlotAddress address;
  address.surface = SlotSurface::PaletteGrid;
  address.paletteKind = GridKind;
  address.entryId = entry.Id;
  Session->BeginDragFromSlot(address, entry);
}

void UCreativePaletteScreen::RegisterGridIcons()
{
  const auto end = std::min(GetGridEntryCount(), GridRegisteredIcons + kGridChunkSize);
  for (; GridRegisteredIcons < end; ++GridRegisteredIcons)
  {
    auto &slot = GridSlotAt(GridRegisteredIcons);
    const auto &id = GridViews[GridRegisteredIcons].ref.Id;
    GLuint texture = 0;
    if (GridKind == ContentKind::Block)
    {
      texture = Icons->GetBlockIconTextureIfCached(id);
      if (!texture)
        Icons->QueueBlockIcon(id);
    }
    else if (GridKind == ContentKind::Item)
      texture = Icons->GetItemIconTexture(id);
    else if (GridKind == ContentKind::Object)
      texture = Icons->GetObjectIconTextureIfCached(id);
    if (texture)
      slot.SetIconTexture(texture);
  }
}

std::pair<size_t, size_t> UCreativePaletteScreen::VisibleGridRange() const
{
  if (!Scroll || !Theme || GetGridEntryCount() == 0)
    return {};
  const auto viewport = Scroll->GetBounds();
  if (viewport.H <= 0)
    return {};
  const auto content = Scroll->Content().GetBounds();
  const int stride = std::max(1, Theme->HotbarSlotSize + Theme->HotbarSlotGap);
  const size_t columns = SlotGridColumnCount(content.W, Theme->HotbarSlotSize, Theme->HotbarSlotGap, 0);
  const int top = std::max(0, viewport.Y - content.Y);
  const auto first = std::min(GetGridEntryCount(), size_t(top / stride) * columns);
  const auto end = std::min(GetGridEntryCount(), size_t((top + viewport.H - 1) / stride + 1) * columns);
  return {first, end};
}

void UCreativePaletteScreen::RefreshGridIconTextures()
{
  const auto count = GetGridEntryCount();
  if (!count || (GridKind != ContentKind::Block && GridKind != ContentKind::Item && GridKind != ContentKind::Object))
    return;
  const auto refresh = [this](size_t i) {
    auto &slot = GridSlotAt(i);
    if (slot.GetIconTexture())
      return;
    const auto &id = GridViews[i].ref.Id;
    const auto texture = GridKind == ContentKind::Block ? Icons->GetBlockIconTextureIfCached(id)
                       : GridKind == ContentKind::Item ? Icons->GetItemIconTexture(id)
                                                       : Icons->GetObjectIconTextureIfCached(id);
    if (texture)
      slot.SetIconTexture(texture);
  };
  const auto [first, end] = VisibleGridRange();
  for (size_t i = first; i < end; ++i)
    refresh(i);
  for (size_t n = 0; n < std::min(count, kBackgroundIconChecks); ++n)
  {
    const auto i = GridCacheCursor % count;
    if (i < first || i >= end)
      refresh(i);
    GridCacheCursor = (i + 1) % count;
  }
}

size_t UCreativePaletteScreen::GetGridIconCount() const
{
  return std::count_if(GridChildren().begin(), GridChildren().end(), [](const auto &widget) {
    return static_cast<const GridSlot &>(*widget).GetIconTexture() != 0;
  });
}

PaletteGridStorage UCreativePaletteScreen::InspectGridStorage(
    const IUContentCatalog &catalog, const UGameSession &session) const noexcept
{
  PaletteGridStorage out;
  if (&catalog != Catalog || &session != Session || !Context || Theme != &Context->GetTheme() ||
      !Root || !Panel || !Scroll ||
      std::none_of(Root->GetChildren().begin(), Root->GetChildren().end(),
                   [this](const auto &widget) { return widget.get() == Panel; }) ||
      std::none_of(Panel->GetChildren().begin(), Panel->GetChildren().end(),
                   [this](const auto &widget) { return widget.get() == Scroll; }))
    return out;

  out.rows = GridViews.size();
  out.widgets = GetGridEntryCount();
  out.kind = GridKind;
  out.current = Built;
  out.complete = IsGridComplete();
  if (out.widgets > out.rows || GridRegisteredIcons > out.widgets)
    return out;
  StlSourceStorage rows, rowText, children, widgets, widgetText, callbacks, total;
  rows.Buffer(GridViews.capacity(), sizeof(InventoryEntryView));
  children.Buffer(GridChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto &row : GridViews)
  {
    rowText.Text(row.ref.Id);
    rowText.Text(row.ref.contentsId);
    rowText.Text(row.label);
  }
  const bool profile = StlSourceStorage::SupportedProfile();
  bool known = profile;
  for (size_t i = 0; i < out.widgets; ++i)
  {
    const auto &widget = GridChildren()[i];
    if (!widget || typeid(*widget) != typeid(GridSlot) || !widget->GetChildren().empty())
      return out;
    const auto &slot = GridSlotAt(i);
    if (slot.Theme != Theme || !GridViews[i].label.empty())
      return out;
    widgets.Add(sizeof(GridSlot));
    children.Buffer(slot.GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    widgetText.Text(slot.Label);
    widgetText.Text(slot.CornerHint);
    widgetText.Text(slot.CountHint);
    widgetText.Text(slot.SpawnHint);
    const auto click = InspectKnownOwnerCallback<GridClickAction>(slot.OnClick, this, profile);
    const auto drag = InspectKnownOwnerCallback<GridDragAction>(slot.OnBeginDrag, this, profile);
    known &= click.supported && click.present && slot.OnClick.target<GridClickAction>()->index == i &&
             drag.supported && drag.present && slot.OnBeginDrag.target<GridDragAction>()->index == i &&
             !slot.OnActivate;
    out.callbacks += size_t(click.present) + size_t(drag.present) + size_t(bool(slot.OnActivate));
    callbacks.Add(click.requests);
    callbacks.Add(drag.requests);
  }
  out.row_bytes = rows.bytes.requested;
  out.row_text_bytes = rowText.bytes.requested;
  out.child_bytes = children.bytes.requested;
  out.widget_bytes = widgets.bytes.requested;
  out.widget_text_bytes = widgetText.bytes.requested;
  out.callback_bytes = callbacks.bytes.requested;
  out.supported = known;
  for (const auto &part : {rows.bytes, rowText.bytes, children.bytes, widgets.bytes, widgetText.bytes, callbacks.bytes})
  {
    total.Add(part.requested);
    out.supported &= part.supported;
  }
  out.requested = total.bytes.requested;
  out.supported &= total.bytes.supported;
  return out;
}

size_t UCreativePaletteScreen::WarmVisibleGridIcons()
{
  if (GridKind == ContentKind::Item)
    return 0;
  const auto [first, end] = VisibleGridRange();
  const auto count = end - first;
  size_t work = 0;
  for (size_t scanned = 0; scanned < count && work < 4; ++scanned)
  {
    const size_t i = first + GridIconCursor++ % count;
    auto &slot = GridSlotAt(i);
    if (slot.GetIconTexture() || !slot.GetBounds().Intersects(Scroll->GetBounds()))
      continue;
    ++work;
    const auto &id = GridViews[i].ref.Id;
    const auto texture = GridKind == ContentKind::Block ? Icons->GetBlockIconTexture(id)
                       : GridKind == ContentKind::Object ? Icons->GetObjectIconTexture(id)
                       : GridKind == ContentKind::UCreature ? Icons->GetCreatureIconTexture(id)
                                                           : Icons->GetSkinIconTexture(id);
    slot.SetIconTexture(texture);
  }
  return work;
}

void UCreativePaletteScreen::LayoutGridInScroll()
{
  if (!Scroll || !Theme)
  {
    return;
  }
  const GuiRect contentRect = Scroll->Content().GetBounds();
  const int slotSize = Theme->HotbarSlotSize;
  const int gap = Theme->HotbarSlotGap;
  const int viewportW = contentRect.W;
  const int viewportH = std::max(1, Scroll->GetBounds().H);
  const int cols = SlotGridColumnCount(viewportW, slotSize, gap, 0);
  const int contentTop = contentRect.Y;

  int x = contentRect.X;
  int y = contentTop;
  for (size_t i = 0; i < GetGridEntryCount(); ++i)
  {
    auto &slot = GridSlotAt(i);
    slot.SetBounds({x, y, slotSize, slotSize});
    slot.SetDrawClip(Scroll->GetBounds());
    if ((i + 1) % static_cast<size_t>(cols) == 0)
    {
      x = contentRect.X;
      y += slotSize + gap;
    }
    else
    {
      x += slotSize + gap;
    }
  }

  const int rows = static_cast<int>((GetGridEntryCount() + cols - 1) / cols);
  const int contentHeight =
      std::max(viewportH, rows * (slotSize + gap) - (rows > 0 ? gap : 0) + 8);
  Scroll->Content().SetBounds(
      {contentRect.X, contentTop, viewportW, contentHeight});
}

std::string UCreativePaletteScreen::GetSectionStateKey() const
{
  return std::to_string(int(Kind)) + "|" + ActiveTypeId + "|" +
         SelectedEntryId + "|" + std::to_string(Scroll->GetScrollY()) + "|" +
         (PreviewDock ? PreviewDock->GetViewStateKey() : "");
}
GuiRect UCreativePaletteScreen::GetPreviewBounds() const
{
  return PreviewDock ? PreviewDock->GetViewportBounds() : GuiRect{};
}
} // namespace cutum
