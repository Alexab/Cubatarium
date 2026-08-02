#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace cutum
{

namespace
{

constexpr int kSheetW = 360;
constexpr int kSheetH = 420;

} // namespace

UCharacterSheetScreen::UCharacterSheetScreen(IUCharacterStatsViewModel *stats)
    : Stats(stats)
{
}

UCharacterSheetScreen::~UCharacterSheetScreen() = default;

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
  int cy = client.Y + 4;
  if (TitleMeta)
  {
    TitleMeta->SetBounds({client.X + 8, cy, client.W - 16, line});
    cy += line;
  }
  if (ModeLabel)
  {
    ModeLabel->SetBounds({client.X + 8, cy, client.W - 16, line});
    cy += line + 6;
  }
  for (UGuiLabel *lab : VitalLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({client.X + 8, cy, client.W - 16, line});
    cy += line;
  }
  cy += 8;
  for (UGuiLabel *lab : AttrLabels)
  {
    if (!lab)
    {
      continue;
    }
    lab->SetBounds({client.X + 8, cy, client.W - 16, line});
    cy += line;
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
      ModeLabel->SetText("Mode: Survival");
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
