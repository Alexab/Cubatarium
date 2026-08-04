#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Interfaces/IUHotbarViewModel.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Game/GameSession.h"
#include "Game/Inventory/SlotInteraction.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

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
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (!Visible && PreviewTexture != 0)
  {
    glDeleteTextures(1, &PreviewTexture);
    PreviewTexture = 0;
    LastPreviewSize = 0;
    LastPreviewYaw = 0.f;
    LastPreviewPitch = 0.f;
    LastCachedTypeId.clear();
    LastCachedSkinId.clear();
    if (PreviewViewport)
    {
      PreviewViewport->SetPreviewTexture(0);
    }
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
  body->SetDrawBackground(false);

  auto meta = std::make_unique<UGuiLabel>(&theme, "");
  TitleMeta = meta.get();
  body->AddChild(std::move(meta));

  auto mode = std::make_unique<UGuiLabel>(&theme, "");
  ModeLabel = mode.get();
  ModeLabel->SetUseSecondaryColor(true);
  body->AddChild(std::move(mode));

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
    const char *armorNames[6] = {"Head", "Chest", "Arms",
                                 "Hands", "Legs", "Feet"};
    for (int i = 0; i < 6; ++i)
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
      ArmorSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }
    for (int i = 0; i < 2; ++i)
    {
      auto slot = std::make_unique<UGuiSlot>(&theme);
      slot->SetLabel(i == 0 ? "Main (=hotbar)" : "Offhand");
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
          const size_t slot = session->GetSelectedSlot(0);
          session->ApplyPendingAssignment(0, slot);
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
      }
      ToolSlots.push_back(slot.get());
      DollPanel->AddChild(std::move(slot));
    }

    body->AddChild(std::move(doll));
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

  const int sheetW = std::min(kSheetW, std::max(320, ViewportW - 16));
  const int sheetH = std::min(kSheetH, std::max(360, ViewportH - 16));
  const int x = std::max(8, (ViewportW - sheetW) / 2);
  const int y = std::max(8, (ViewportH - sheetH) / 2);
  Window->SetBounds({x, y, sheetW, sheetH});
  const GuiRect client = Window->GetClientArea();

  int cy = client.Y + 4;
  if (TitleMeta)
  {
    TitleMeta->SetBounds({client.X + 8, cy, client.W - 16, kLineH});
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
  const int dollW = std::max(220, (client.W * 58) / 100);
  const int statsW = std::max(160, client.W - dollW - gap - 16);
  const int dollX = client.X + 8;
  const int statsX = dollX + dollW + gap;

  if (DollPanel)
  {
    DollPanel->SetBounds({dollX, cy, dollW, contentH});
  }
  if (StatsPanel)
  {
    StatsPanel->SetBounds({statsX, cy, statsW, contentH});
  }

  // --- Paper-doll interior ---
  //        [Head]
  // [Chest][PREVIEW][Arms]
  // [Legs]          [Hands]
  //   [Tool1][Feet][Tool2]
  const int pad = 8;
  const int headerH = kLineH;
  if (EquipmentHeader)
  {
    EquipmentHeader->SetBounds({dollX + pad, cy + pad, dollW - pad * 2, headerH});
  }

  const int slotColW = kSlotSize + kSlotGap;
  const int previewSide = std::min(
      180, std::max(110, dollW - pad * 2 - slotColW * 2 - 8));
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
  placeArmor(0, headX, headY);                                   // Head
  placeArmor(1, leftSlotX, midY - kSlotSize / 2 - 4);             // Chest
  placeArmor(4, leftSlotX, midY + kSlotSize / 2 + 4);             // Legs
  placeArmor(2, rightSlotX, midY - kSlotSize / 2 - 4);            // Arms
  placeArmor(3, rightSlotX, midY + kSlotSize / 2 + 4);            // Hands

  const int bottomY = previewTop + previewSide + kSlotGap;
  const int feetX = previewX + (previewSide - kSlotSize) / 2;
  placeArmor(5, feetX, bottomY);                                 // Feet

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

  // --- Stats interior ---
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
}

bool UCharacterSheetScreen::PickArmorSlot(int x, int y, size_t &outSlot) const
{
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
  if (ToolSlots.empty() || !ToolSlots[0])
  {
    return false;
  }
  return PointInBounds(x, y, ToolSlots[0]->GetBounds());
}

bool UCharacterSheetScreen::PickOffhandSlot(int x, int y) const
{
  if (ToolSlots.size() < 2 || !ToolSlots[1])
  {
    return false;
  }
  return PointInBounds(x, y, ToolSlots[1]->GetBounds());
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
    for (int i = 0; i < 6 && i < static_cast<int>(ArmorSlots.size()); ++i)
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
  }

  PreviewValid = snap.valid && !snap.typeId.empty();
  PreviewTypeId = snap.typeId;
  PreviewSkinId = snap.skinId;
  RenderCharacterPreview();
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

  const float yaw = PreviewViewport->GetYaw();
  const float pitch = PreviewViewport->GetPitch();
  const bool sameCache =
      PreviewTexture != 0 && size == LastPreviewSize &&
      PreviewTypeId == LastCachedTypeId && PreviewSkinId == LastCachedSkinId &&
      std::abs(yaw - LastPreviewYaw) < 0.01f &&
      std::abs(pitch - LastPreviewPitch) < 0.01f;
  if (sameCache)
  {
    return;
  }

  const GLuint tex = CreaturePreview->RenderToUniqueTexture(
      PreviewTypeId, PreviewSkinId, size, yaw, pitch);
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
