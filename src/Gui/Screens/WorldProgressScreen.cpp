#include "Gui/Screens/WorldProgressScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiProgressBar.h"
#include "Gui/Layout/GuiLayout.h"
#include <algorithm>

namespace cutum
{

namespace
{

const char *TitleForKind(WorldOperationKind kind)
{
  switch (kind)
  {
  case WorldOperationKind::Load:
    return "Loading world...";
  case WorldOperationKind::Create:
    return "Creating world...";
  case WorldOperationKind::Save:
    return "Saving world...";
  case WorldOperationKind::EnterGame:
    return "Starting game...";
  }
  return "Please wait...";
}

} // namespace

void UWorldProgressScreen::Build(UGuiContext &ctx)
{
  auto backdrop = std::make_unique<UGuiPanel>(&ctx.GetTheme());
  backdrop->SetDrawBackground(true);
  backdrop->SetBounds(GuiRect{0, 0, ViewportW, ViewportH});

  auto card = std::make_unique<UGuiPanel>(&ctx.GetTheme());
  card->SetStackLayout(10, 16);
  card->SetDrawBackground(true);

  auto title = std::make_unique<UGuiLabel>(&ctx.GetTheme(), TitleForKind(
                                                              WorldOperationKind::Load));
  title->SetTextAlign(GuiTextAlign::Center);
  TitleLabel = title.get();
  card->AddChild(std::move(title));

  auto phase = std::make_unique<UGuiLabel>(&ctx.GetTheme(), "");
  phase->SetTextAlign(GuiTextAlign::Center);
  phase->SetUseSecondaryColor(true);
  PhaseLabel = phase.get();
  card->AddChild(std::move(phase));

  auto bar = std::make_unique<UGuiProgressBar>(&ctx.GetTheme());
  bar->SetIndeterminate(true);
  bar->SetShowPercent(true);
  ProgressBar = bar.get();
  card->AddChild(std::move(bar));

  card->SetBounds(GuiRect{0, 0, CardW, CardH});
  backdrop->AddChild(std::move(card));
  Root = std::move(backdrop);
  LayoutCentered();
}

void UWorldProgressScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  LayoutCentered();
}

void UWorldProgressScreen::LayoutCentered()
{
  if (!Root || Root->GetChildren().empty())
  {
    return;
  }
  Root->SetBounds(GuiRect{0, 0, ViewportW, ViewportH});
  UGuiWidget *card = Root->GetChildren().front().get();
  const int x = std::max(0, (ViewportW - CardW) / 2);
  const int y = std::max(0, (ViewportH - CardH) / 2);
  card->SetBounds(GuiRect{x, y, CardW, CardH});

  std::vector<UGuiWidget *> stackChildren;
  stackChildren.reserve(card->GetChildren().size());
  for (const auto &child : card->GetChildren())
  {
    if (child->IsVisible())
    {
      stackChildren.push_back(child.get());
    }
  }
  UGuiLayout::StackVertical(card->GetBounds(), 10, 16, stackChildren);
}

void UWorldProgressScreen::ApplySnapshot(const ProgressSnapshot &snapshot)
{
  if (TitleLabel)
  {
    TitleLabel->SetText(TitleForKind(snapshot.kind));
  }
  if (PhaseLabel)
  {
    PhaseLabel->SetText(snapshot.message.empty() ? " " : snapshot.message);
  }
  if (ProgressBar)
  {
    if (snapshot.fraction < 0.f)
    {
      ProgressBar->SetIndeterminate(true);
    }
    else
    {
      ProgressBar->SetValue(snapshot.fraction);
    }
  }
}

} // namespace cutum
