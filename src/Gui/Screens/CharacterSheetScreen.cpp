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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

constexpr int kSheetW = 360;
constexpr int kSheetH = 420;

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
    if (PreviewViewport)
    {
      PreviewViewport->SetPreviewTexture(0);
    }
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
  body->AddChild(std::move(mode));

  // Preview + equipment slots (placed in Relayout()).
  {
    auto preview = std::make_unique<UGuiPreviewViewport>(&theme);
    PreviewViewport = preview.get();
    PreviewViewport->SetOnRotationChanged([this](float /*yaw*/, float /*pitch*/) {
      RenderCharacterPreview();
    });
    body->AddChild(std::move(preview));
  }

  ArmorSlots.clear();
  ToolSlots.clear();
  const char *armorNames[6] = {"Head", "Chest", "Arms", "Hands",
                               "Legs", "Feet"};
  for (int i = 0; i < 6; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(&theme);
    slot->SetLabel(armorNames[i]);
    slot->SetIconTexture(0);
    ArmorSlots.push_back(slot.get());
    body->AddChild(std::move(slot));
  }
  for (int i = 0; i < 2; ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(&theme);
    slot->SetLabel(i == 0 ? "Tool (1)" : "Tool (2)");
    slot->SetIconTexture(0);
    ToolSlots.push_back(slot.get());
    body->AddChild(std::move(slot));
  }

  VitalLabels.clear();
  for (int i = 0; i < 6; ++i)
  {
    auto lab = std::make_unique<UGuiLabel>(&theme, "");
    VitalLabels.push_back(lab.get());
    body->AddChild(std::move(lab));
  }

  AttrLabels.clear();
  for (int i = 0; i < 7; ++i)
  {
    auto lab = std::make_unique<UGuiLabel>(&theme, "");
    AttrLabels.push_back(lab.get());
    body->AddChild(std::move(lab));
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
  const int x = std::max(8, (ViewportW - kSheetW) / 2);
  const int y = std::max(8, (ViewportH - kSheetH) / 2);
  Window->SetBounds({x, y, kSheetW, kSheetH});
  const GuiRect client = Window->GetClientArea();
  const int line = 22;

  // Two-column layout:
  // - left: text (title/mode/vitals/attributes)
  // - right: 3D preview + armor/tool slots
  const int rightW = std::min(160, std::max(120, client.W - 16));
  const int rightX = client.X + client.W - rightW - 8;
  const int leftX = client.X + 8;
  const int leftW = std::max(80, rightX - leftX - 4);

  int cy = client.Y + 4;
  if (TitleMeta)
  {
    TitleMeta->SetBounds({leftX, cy, leftW, line});
    cy += line;
  }
  if (ModeLabel)
  {
    ModeLabel->SetBounds({leftX, cy, leftW, line});
    cy += line + 6;
  }
  for (UGuiLabel *lab : VitalLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({leftX, cy, leftW, line});
    cy += line;
  }
  cy += 8;
  for (UGuiLabel *lab : AttrLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({leftX, cy, leftW, line});
    cy += line;
  }

  // Preview viewport.
  if (PreviewViewport)
  {
    constexpr int previewH = 160;
    PreviewViewport->SetBounds({rightX, client.Y + 8, rightW, previewH});
  }

  // Slots.
  constexpr int previewH = 160;
  constexpr int slotGap = 6;
  constexpr int slotCols = 2;
  const int slotSize =
      std::max(44, std::min(60, (rightW - slotGap) / slotCols));
  const int slotRowH = slotSize + slotGap;
  const int slotStartX =
      rightX + std::max(0, (rightW - (slotCols * slotSize + slotGap)) / 2);
  const int slotStartY = client.Y + 8 + previewH + 8;

  for (int i = 0; i < static_cast<int>(ArmorSlots.size()); ++i)
  {
    if (!ArmorSlots[i])
    {
      continue;
    }
    const int row = i / slotCols;
    const int col = i % slotCols;
    ArmorSlots[i]->SetBounds({slotStartX + col * slotRowH, slotStartY + row * slotRowH,
                               slotSize, slotSize});
  }

  // Tools: next row after armor grid.
  const int toolRow = 3;
  for (int i = 0; i < static_cast<int>(ToolSlots.size()); ++i)
  {
    if (!ToolSlots[i])
    {
      continue;
    }
    const int col = i % slotCols;
    ToolSlots[i]->SetBounds({slotStartX + col * slotRowH,
                              slotStartY + toolRow * slotRowH, slotSize,
                              slotSize});
  }
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
  if (VitalLabels.size() >= 6 && snap.valid)
  {
    const auto &v = snap.vitals;
    VitalLabels[0]->SetText(FormatBar("Health", v.health, v.maxHealth));
    VitalLabels[1]->SetText(FormatBar("Satiety", v.satiety, v.maxSatiety));
    VitalLabels[2]->SetText(FormatBar("Thirst", v.thirst, v.maxThirst));
    VitalLabels[3]->SetText(FormatBar("Fatigue", v.fatigue, v.maxFatigue));
    VitalLabels[4]->SetText(FormatBar("Breath", v.breath, v.maxBreath));
    char wounds[64];
    std::snprintf(wounds, sizeof(wounds), "Fatal wounds: %d / %d  Armor: %.0f",
                  v.fatalWounds, v.maxFatalWounds, v.armor);
    VitalLabels[5]->SetText(wounds);
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

  // Update equipment slots from snapshot (armor + 2 active tools).
  if (Icons)
  {
    for (int i = 0; i < 6 && i < static_cast<int>(ArmorSlots.size());
         ++i)
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
        ToolSlots[i]->SetIconTexture(Icons->GetItemIconTexture(s.itemId));
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

  // Update character preview.
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
  const bool sameAngles =
      std::abs(yaw - LastPreviewYaw) < 0.01f &&
      std::abs(pitch - LastPreviewPitch) < 0.01f &&
      size == LastPreviewSize;
  if (PreviewTexture != 0 && sameAngles)
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
