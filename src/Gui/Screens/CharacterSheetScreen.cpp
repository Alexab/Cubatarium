#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Gui/Interfaces/IUHotbarViewModel.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Game/GameSession.h"
#include "World/Core/World.h"
#include "World/Persistence/WorldPersistence.h"
#include "Creatures/Player/User.h"
#include "Game/Inventory/SlotInteraction.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <fstream>
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
  if(visible != Visible) { EditingAppearance=false; ResetAppearanceDraft(); }
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

void UCharacterSheetScreen::Toggle() { SetVisible(!Visible); }

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

  auto window = std::make_unique<UGuiWindow>(&theme, "Character");
  Window = window.get();

  auto body = std::make_unique<UGuiPanel>(&theme);
  BodyPanel=body.get();
  body->SetDrawBackground(false);

  auto meta = std::make_unique<UGuiLabel>(&theme, "");
  TitleMeta = meta.get();
  body->AddChild(std::move(meta));

  auto mode = std::make_unique<UGuiLabel>(&theme, "");
  ModeLabel = mode.get();
  ModeLabel->SetUseSecondaryColor(true);
  body->AddChild(std::move(mode));

  auto toggle=std::make_unique<UGuiButton>(&theme,"Appearance");
  AppearanceToggle=toggle.get();
  toggle->SetOnClick([this]{ EditingAppearance=!EditingAppearance;ResetAppearanceDraft();Relayout();RefreshLabels(); });
  body->AddChild(std::move(toggle));
  auto customization=std::make_unique<UGuiPanel>(&theme);
  AppearancePanel=customization.get();
  for(int row=0;row<6;++row) {
    auto button=std::make_unique<UGuiButton>(&theme,"");
    AppearanceButtons.push_back(button.get());
    button->SetOnClick([this,row]{
      auto cycle=[](auto options,std::string& value){size_t i=0;while(i<options.size()&&value!=options[i])++i;value=options[(i+1)%options.size()];};
      if(row==0) cycle(HumanAppearance::Bodies,DraftAppearance.bodyVariant);
      if(row==1) { const std::array<const char*,3> outfits{"","human_adventurer","human_guard"};cycle(outfits,DraftSkin); }
      if(row==2) cycle(HumanAppearance::Hairstyles,DraftAppearance.hairstyleId);
      if(row==3) cycle(HumanAppearance::Colors,DraftAppearance.hairColorId);
      if(row==4 && Stats && !Stats->ApplyHumanAppearance(DraftAppearance,DraftSkin)) return;
      if(row>=4) {EditingAppearance=false;ResetAppearanceDraft();Relayout();}
      RefreshAppearanceControls();RefreshLabels();
    });
    customization->AddChild(std::move(button));
  }
  body->AddChild(std::move(customization));

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
    PreviewViewport->SetOnRotationChanged(
        [this](float /*yaw*/, float /*pitch*/) { RenderCharacterPreview(); });
    DollPanel->AddChild(std::move(preview));

    ArmorSlots.clear();
    ToolSlots.clear();
    const char *armorNames[4] = {"Head", "Chest", "Legs", "Feet"};
    for (int i = 0; i < 4; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetLabel(armorNames[i]);
      slot->SetIconTexture(0);
      const size_t armorIndex = static_cast<size_t>(i);
      SlotAddress address;
      address.surface = SlotSurface::CharacterArmor;
      address.slot = armorIndex;
      slot->SetOnBeginDrag([this, address, armorIndex]() {
        auto *session = dynamic_cast<UGameSession *>(Stats);
        if (!session)
        {
          return;
        }
        const InventoryEntryRef entry = session->GetArmorEntryRef(armorIndex);
        if (!entry.empty)
        {
          session->BeginDragFromSlot(address, entry);
        }
      });
      slot->SetOnClick([this, address, armorIndex]() {
        auto *session = dynamic_cast<UGameSession *>(Stats);
        if (!session)
        {
          return;
        }
        const InventoryEntryRef entry = session->GetArmorEntryRef(armorIndex);
        if (entry.empty)
        {
          return;
        }
        session->BeginDragFromSlot(address, entry);
        SlotAddress bag;
        bag.surface = SlotSurface::PaletteGrid;
        session->DropOnSlot(bag);
      });
      ArmorSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }
    for (int i = 0; i < 2; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetLabel(i == 0 ? "Hand" : "Off");
      slot->SetIconTexture(0);
      if (i == 0)
      {
        slot->SetOnBeginDrag([this]() {
          auto *session = dynamic_cast<UGameSession *>(Stats);
          if (!session)
          {
            return;
          }
          SlotAddress address;
          address.surface = SlotSurface::Hotbar;
          address.bar = 0;
          address.slot = session->GetSelectedSlot(0);
          const InventoryEntryRef entry =
              session->GetHotbarEntryRef(address.bar, address.slot);
          if (!entry.empty)
          {
            session->BeginDragFromSlot(address, entry);
          }
        });
        slot->SetOnClick([this]() {
          auto *session = dynamic_cast<UGameSession *>(Stats);
          if (!session)
          {
            return;
          }
          SlotAddress address;
          address.surface = SlotSurface::Hotbar;
          address.bar = 0;
          address.slot = session->GetSelectedSlot(0);
          const InventoryEntryRef entry =
              session->GetHotbarEntryRef(address.bar, address.slot);
          if (!entry.empty)
          {
            session->BeginDragFromSlot(address, entry);
            SlotAddress bag;
            bag.surface = SlotSurface::PaletteGrid;
            session->DropOnSlot(bag);
            return;
          }
          session->ApplyPendingAssignment(0, address.slot);
        });
      }
      else
      {
        SlotAddress address;
        address.surface = SlotSurface::CharacterOffhand;
        slot->SetOnBeginDrag([this, address]() {
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
        slot->SetOnClick([this, address]() {
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
          SlotAddress bag;
          bag.surface = SlotSurface::PaletteGrid;
          session->DropOnSlot(bag);
        });
      }
      ToolSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }

    body->AddChild(std::move(doll));
  }

  {
    auto bag = std::make_unique<UGuiPanel>(&theme);
    BagPanel = bag.get();
    BagPanel->SetDrawBackground(true);
    BagSlots.clear();
    for (int i = 0; i < 24; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetIconTexture(0);
      const size_t bagIndex = static_cast<size_t>(i);
      slot->SetOnBeginDrag([this, bagIndex]() {
        auto *session = dynamic_cast<UGameSession *>(Stats);
        if (!session)
        {
          return;
        }
        const CharacterStatsSnapshot snap = session->GetCharacterStatsSnapshot();
        if (bagIndex >= snap.carriedItems.size())
        {
          return;
        }
        InventoryEntryRef entry;
        entry.empty = false;
        entry.kind = InventoryEntryKind::Item;
        entry.Id = snap.carriedItems[bagIndex].id;
        entry.count = snap.carriedItems[bagIndex].count;
        SlotAddress address;
        address.surface = SlotSurface::PaletteGrid;
        address.entryId = entry.Id;
        session->BeginDragFromSlot(address, entry);
      });
      slot->SetOnClick([this, bagIndex]() {
        auto *session = dynamic_cast<UGameSession *>(Stats);
        if (!session)
        {
          return;
        }
        const CharacterStatsSnapshot snap = session->GetCharacterStatsSnapshot();
        if (bagIndex >= snap.carriedItems.size())
        {
          return;
        }
        InventoryEntryRef entry;
        entry.empty = false;
        entry.kind = InventoryEntryKind::Item;
        entry.Id = snap.carriedItems[bagIndex].id;
        entry.count = snap.carriedItems[bagIndex].count;
        SlotAddress address;
        address.surface = SlotSurface::PaletteGrid;
        address.entryId = entry.Id;
        session->BeginDragFromSlot(address, entry);
        SlotAddress doll;
        doll.surface = SlotSurface::CharacterDoll;
        if (!session->DropOnSlot(doll))
        {
          session->BeginDragFromSlot(address, entry);
          SlotAddress hand;
          hand.surface = SlotSurface::Hotbar;
          hand.bar = 0;
          hand.slot = session->GetSelectedSlot(0);
          session->DropOnSlot(hand);
        }
      });
      BagSlots.push_back(slot.get());
      BagPanel->AddChild(std::move(slot));
    }
    body->AddChild(std::move(bag));
  }

  // Stats panel (vitals + attributes).
  {
    auto stats = std::make_unique<UGuiPanel>(&theme);
    StatsPanel = stats.get();
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

    body->AddChild(std::move(stats));
  }

  window->AddChild(std::move(body));
  root->AddChild(std::move(window));
  Root = std::move(root);
  Built = true;
  Relayout();
  RefreshLabels();
  SetVisible(false);
}

void UCharacterSheetScreen::Relayout()
{
  if (!Root || !Window)
  {
    return;
  }
  Root->SetBounds({0, 0, ViewportW, ViewportH});

  WideLayout = ViewportW >= 900 && ViewportH >= 640;
  const int sheetW = WideLayout ? std::min(ViewportW - 24, 980)
                                : std::min(kSheetW, std::max(320, ViewportW - 16));
  const int sheetH = WideLayout ? std::min(ViewportH - 24, 640)
                                : std::min(ViewportH - 16, std::max(420, ViewportH - 16));
  const int x = std::max(8, (ViewportW - sheetW) / 2);
  const int y = std::max(8, (ViewportH - sheetH) / 2);
  Window->SetBounds({x, y, sheetW, sheetH});
  const GuiRect client = Window->GetClientArea();
  if(BodyPanel) BodyPanel->SetBounds(client);

  int cy = client.Y + 4;
  if (TitleMeta)
  {
    TitleMeta->SetBounds({client.X + 8, cy, client.W - 160, kLineH});
    AppearanceToggle->SetBounds({client.X+client.W-150,cy,140,24});
    cy += kLineH;
  }
  if (ModeLabel)
  {
    ModeLabel->SetBounds({client.X + 8, cy, client.W - 16, kLineH});
    cy += kLineH + 6;
  }

  // Two columns under identity: doll (left ~58%) + stats (right ~40%).
  const int gap = 8;
  const int contentH = client.Y + client.H - cy - 8;
  const int dollW = WideLayout ? std::min(380, (client.W * 46) / 100)
                              : std::max(200, client.W - 16);
  const int dollH = WideLayout ? (EditingAppearance ? contentH : (contentH * 62) / 100) : std::min(contentH / 2, 300);
  const int dollX = client.X + 8;
  const int bagX = WideLayout ? dollX + dollW + gap : dollX;
  const int bagY = WideLayout ? cy : cy + dollH + gap;
  const int bagW = WideLayout ? std::max(160, client.W - dollW - gap - 16)
                             : dollW;
  const int bagH = WideLayout ? contentH : std::max(80, contentH - dollH - gap);

  if (DollPanel)
  {
    DollPanel->SetBounds({dollX, cy, dollW, dollH});
  }
  if (BagPanel)
  {
    BagPanel->SetBounds({bagX, bagY, bagW, bagH});
  }
  if (StatsPanel)
  {
    if (WideLayout)
    {
      StatsPanel->SetVisible(true);
      StatsPanel->SetBounds(
          {dollX, cy + dollH + gap, dollW, std::max(80, contentH - dollH - gap)});
    }
    else
    {
      StatsPanel->SetVisible(false);
    }
  }

  if(AppearancePanel) {
    AppearancePanel->SetBounds({bagX,bagY,bagW,bagH});
    AppearancePanel->SetVisible(EditingAppearance);
    if(EditingAppearance) {BagPanel->SetVisible(false);StatsPanel->SetVisible(false);}
    else BagPanel->SetVisible(true);
    const int rowHeight=std::min(34,std::max(22,(bagH-16)/6));
    for(size_t i=0;i<AppearanceButtons.size();++i)
      AppearanceButtons[i]->SetBounds({bagX+8,bagY+8+int(i)*rowHeight,bagW-16,rowHeight-4});
  }
  // --- Paper-doll interior ---
  //        [Head]
  // [Chest][PREVIEW][Legs]
  //   [Tool1][Feet][Tool2]
  const int pad = 8;
  const int headerH = kLineH;
  if (EquipmentHeader)
  {
    EquipmentHeader->SetBounds({dollX + pad, cy + pad, dollW - pad * 2, headerH});
  }

  const int slotColW = kSlotSize + kSlotGap;
  const int previewCap = WideLayout ? 160 : 96;
  const int previewSide = std::min(
      previewCap, std::max(72, dollW - pad * 2 - slotColW * 2 - 8));
  const int headerBottom = cy + pad + headerH;
  // Reserve a full slot row above the preview for Head (centered).
  const int previewTop = headerBottom + 4 + kSlotSize + kSlotGap;
  const int previewX = dollX + (dollW - previewSide) / 2;
  if (PreviewViewport)
  {
    PreviewViewport->SetBounds(
        {previewX, previewTop, previewSide, previewSide});
  }

  const int leftSlotX = previewX - slotColW - 4;
  const int rightSlotX = previewX + previewSide + 4;
  const int midY = previewTop + previewSide / 2 - kSlotSize / 2;

  auto placeArmor = [&](int index, int sx, int sy) {
    if (index >= 0 && index < static_cast<int>(ArmorSlots.size()) &&
        ArmorSlots[index])
    {
      ArmorSlots[index]->SetBounds({sx, sy, kSlotSize, kSlotSize});
    }
  };

  const int headX = previewX + (previewSide - kSlotSize) / 2;
  const int headY = previewTop - kSlotSize - kSlotGap;
  placeArmor(0, headX, headY);                       // Head
  placeArmor(1, leftSlotX, midY);                    // Chest
  placeArmor(2, rightSlotX, midY);                   // Legs

  const int bottomY = previewTop + previewSide + kSlotGap;
  const int feetX = previewX + (previewSide - kSlotSize) / 2;
  placeArmor(3, feetX, bottomY);                     // Feet

  if (ToolSlots.size() >= 1 && ToolSlots[0])
  {
    ToolSlots[0]->SetBounds(
        {feetX - (kSlotSize + kSlotGap), bottomY, kSlotSize, kSlotSize});
  }
  if (ToolSlots.size() >= 2 && ToolSlots[1])
  {
    ToolSlots[1]->SetBounds(
        {feetX + (kSlotSize + kSlotGap), bottomY, kSlotSize, kSlotSize});
  }

  if(EditingAppearance && PreviewViewport) {
    const int side=std::max(72,std::min(dollW-24,dollH-24));
    PreviewViewport->SetBounds({dollX+(dollW-side)/2,cy+(dollH-side)/2,side,side});
  }
  if(EquipmentHeader)EquipmentHeader->SetVisible(!EditingAppearance);
  for(auto* slot:ArmorSlots)slot->SetVisible(!EditingAppearance);
  for(auto* slot:ToolSlots)slot->SetVisible(!EditingAppearance);

  // Stats sit under the doll on wide screens (same absolute coordinates).
  const int statsX = dollX;
  const int statsW = dollW;
  int sy = cy + pad;
  if (VitalsHeader)
  {
    VitalsHeader->SetBounds({statsX + pad, sy, statsW - pad * 2, headerH});
    sy += headerH + 4;
  }
  for (UGuiLabel *lab : VitalLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({statsX + pad, sy, statsW - pad * 2, kLineH});
    sy += kLineH;
  }
  sy += 10;
  if (AttrsHeader)
  {
    AttrsHeader->SetBounds({statsX + pad, sy, statsW - pad * 2, headerH});
    sy += headerH + 4;
  }
  for (UGuiLabel *lab : AttrLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({statsX + pad, sy, statsW - pad * 2, kLineH});
    sy += kLineH;
  }

  if (BagPanel)
  {
    const GuiRect bag = BagPanel->GetBounds();
    const int slot = WideLayout ? kSlotSize : 40;
    const int gapS = 4;
    const int maxCols = WideLayout ? 5 : 4;
    const int cols = SlotGridColumnCount(bag.W - 8, slot, gapS, maxCols);
    for (size_t i = 0; i < BagSlots.size(); ++i)
    {
      const int col = static_cast<int>(i) % cols;
      const int row = static_cast<int>(i) / cols;
      BagSlots[i]->SetBounds({bag.X + 6 + col * (slot + gapS),
                              bag.Y + 6 + row * (slot + gapS), slot, slot});
    }
  }
}

bool UCharacterSheetScreen::PickArmorSlot(int x, int y, size_t &outSlot) const
{
  if(EditingAppearance)return false;
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
  if(EditingAppearance)return false;
  if (ToolSlots.empty() || !ToolSlots[0])
  {
    return false;
  }
  return PointInBounds(x, y, ToolSlots[0]->GetBounds());
}

bool UCharacterSheetScreen::PickOffhandSlot(int x, int y) const
{
  if(EditingAppearance)return false;
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
  if (ModeLabel)
  {
    if (snap.gameMode == WorldGameMode::Creative)
    {
      ModeLabel->SetText("Mode: Creative (vitals frozen)");
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
    auto setAttr = [](UGuiLabel *lab, const char *name, int value) {
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
      if (ToolSlots[i] && !s.itemId.empty())
      {
        const GLuint tex =
            s.isBlock ? Icons->GetBlockIconTexture(s.itemId)
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
    for (size_t i = 0; i < BagSlots.size(); ++i)
    {
      if (!BagSlots[i])
      {
        continue;
      }
      if (i < snap.carriedItems.size())
      {
        const auto &carried = snap.carriedItems[i];
        BagSlots[i]->SetIconTexture(Icons->GetItemIconTexture(carried.id));
        BagSlots[i]->SetCornerHint(carried.count < 0
                                       ? "inf"
                                       : std::to_string(carried.count));
        BagSlots[i]->SetVisible(true);
      }
      else
      {
        BagSlots[i]->SetIconTexture(0);
        BagSlots[i]->SetCornerHint("");
        BagSlots[i]->SetVisible(i < 8 || !snap.carriedItems.empty());
      }
    }
  }

  PreviewValid = snap.valid && !snap.typeId.empty();
  PreviewTypeId = snap.typeId;
  PreviewSkinId = EditingAppearance ? DraftSkin : snap.skinId;
  RefreshAppearanceControls();
  RenderCharacterPreview();
}

void UCharacterSheetScreen::RunAppearanceQa(const std::string &reportPath) {
  if(!Stats || Stats->GetCharacterStatsSnapshot().typeId!="human")throw std::runtime_error("Appearance UI QA requires controlled human");
  const auto original=Stats->GetCharacterStatsSnapshot();
  SetVisible(true);AppearanceToggle->Activate();
  AppearanceButtons[0]->Activate();AppearanceButtons[1]->Activate();
  AppearanceButtons[2]->Activate();AppearanceButtons[2]->Activate();AppearanceButtons[3]->Activate();
  const auto desired=DraftAppearance;const auto outfit=DraftSkin;
  const bool draftIsolated=Stats->GetCharacterStatsSnapshot().humanAppearance.Key()==original.humanAppearance.Key() && Stats->GetCharacterStatsSnapshot().skinId==original.skinId;
  const bool previewWorks=PreviewTexture!=0;
  AppearanceButtons[4]->Activate();
  auto applied=Stats->GetCharacterStatsSnapshot();
  const bool appliedOk=applied.humanAppearance.Key()==desired.Key() && applied.skinId==outfit;
  bool userSaved=false;
  if(auto* session=dynamic_cast<UGameSession*>(Stats)) {
    const auto world=session->GetWorld();
    const std::string path=reportPath+".users.json";
    UWorldPersistence persistence;persistence.SaveUsers(*world,path);
    nlohmann::json saved;std::ifstream in(path);in>>saved;
    const auto& row=saved.at(world->GetCurrentUserName());
    userSaved=row.at("human_appearance").get<HumanAppearance>().Key()==desired.Key() && row.value("selected_skin_id","")==outfit && world->GetCurrentUser()->GetHumanAppearance().Key()==desired.Key();
  }
  AppearanceToggle->Activate();AppearanceButtons[2]->Activate();AppearanceButtons[5]->Activate();
  const bool cancelled=Stats->GetCharacterStatsSnapshot().humanAppearance.Key()==desired.Key() && Stats->GetCharacterStatsSnapshot().skinId==outfit;
  nlohmann::json layouts=nlohmann::json::array();
  AppearanceToggle->Activate();
  for(auto size:std::array<std::pair<int,int>,2>{{{1280,720},{640,480}}}) {
    OnViewportChanged(size.first,size.second);bool valid=true;
    for(auto* b:AppearanceButtons) {
      const auto rect=b->GetBounds();
      valid=valid && rect.W>0 && rect.H>0 && rect.X>=0 && rect.Y>=0 && rect.X+rect.W<=size.first && rect.Y+rect.H<=size.second &&
        Root->HitTestFocusable(rect.X+rect.W/2,rect.Y+rect.H/2)==b;
    }
    layouts.push_back({{"width",size.first},{"height",size.second},{"controls_reachable",valid}});
  }
  const bool restored=Stats->ApplyHumanAppearance(original.humanAppearance,original.skinId);
  EditingAppearance=false;ResetAppearanceDraft();
  OnViewportChanged(1280,720);RefreshLabels();
  AppearanceToggle->Activate();
  AppearanceButtons[0]->Activate();AppearanceButtons[2]->Activate();AppearanceButtons[2]->Activate();
  bool passed=draftIsolated && previewWorks && appliedOk && cancelled && restored && userSaved;
  for(const auto& layout:layouts)passed=passed && layout["controls_reachable"].get<bool>();
  nlohmann::json report={{"passed",passed},{"draft_isolated",draftIsolated},{"preview_rendered",previewWorks},{"apply",appliedOk},{"cancel",cancelled},{"original_selection_restored",restored},{"user_save_serialized",userSaved},{"layouts",layouts}};
  std::ofstream file(reportPath);file<<report.dump(2)<<'\n';file.close();
  if(!passed)throw std::runtime_error("Appearance UI QA failed");
}

void UCharacterSheetScreen::ResetAppearanceDraft() {
  if(!Stats)return;
  const auto snap=Stats->GetCharacterStatsSnapshot();
  DraftAppearance=snap.humanAppearance;DraftSkin=snap.skinId;
}
void UCharacterSheetScreen::RefreshAppearanceControls() {
  if(!Stats || !AppearanceToggle)return;
  const bool human=Stats->GetCharacterStatsSnapshot().typeId=="human";
  AppearanceToggle->SetVisible(human);
  if(!human && EditingAppearance) {EditingAppearance=false;Relayout();}
  if(AppearanceButtons.size()!=6)return;
  AppearanceButtons[0]->SetLabel(DraftAppearance.bodyVariant=="female" ? "Body: female" : "Body: male");
  AppearanceButtons[1]->SetLabel(DraftSkin=="human_guard" ? "Outfit: guard" : DraftSkin=="human_adventurer" ? "Outfit: traveler" : "Outfit: default");
  const std::array<const char*,6> hair{"No hair","Short","Bob","Long","Ponytail","Curly"};
  const std::array<const char*,5> color{"Black","Chestnut","Brown","Blond","Ginger"};
  for(size_t i=0;i<hair.size();++i)if(DraftAppearance.hairstyleId==HumanAppearance::Hairstyles[i])AppearanceButtons[2]->SetLabel(std::string("Hairstyle: ")+hair[i]);
  for(size_t i=0;i<color.size();++i)if(DraftAppearance.hairColorId==HumanAppearance::Colors[i])AppearanceButtons[3]->SetLabel(std::string("Hair color: ")+color[i]);
  AppearanceButtons[4]->SetLabel("Apply");AppearanceButtons[5]->SetLabel("Cancel");
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
  const auto look=EditingAppearance ? DraftAppearance : Stats->GetCharacterStatsSnapshot().humanAppearance;
  const bool sameCache =
      look.Key()==LastCachedAppearance &&
      PreviewTexture != 0 && size == LastPreviewSize &&
      PreviewTypeId == LastCachedTypeId && PreviewSkinId == LastCachedSkinId &&
      armorKey == LastCachedArmorKey && wieldKey == LastCachedWieldKey &&
      std::abs(yaw - LastPreviewYaw) < 0.01f &&
      std::abs(pitch - LastPreviewPitch) < 0.01f;
  if (sameCache)
  {
    return;
  }

  const GLuint tex = CreaturePreview->RenderToUniqueTexture(
      PreviewTypeId, PreviewSkinId, size, yaw, pitch, 0.f, false, &armorSlots,
      mainId.empty() ? nullptr : &mainId, offId.empty() ? nullptr : &offId, &look);
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
    RefreshLabels();
  }
}

void UCharacterSheetScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

} // namespace cutum
