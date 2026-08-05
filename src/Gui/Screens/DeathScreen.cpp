#include "Gui/Screens/DeathScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"

namespace cutum
{

void UDeathScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(50);
  Panel = panel.get();

  auto title = std::make_unique<UGuiLabel>(Theme, "You Died");
  Title = title.get();
  Panel->AddChild(std::move(title));

  auto cause = std::make_unique<UGuiLabel>(Theme, "");
  cause->SetUseSecondaryColor(true);
  CauseLabel = cause.get();
  Panel->AddChild(std::move(cause));

  auto respawn = std::make_unique<UGuiButton>(Theme, "Respawn");
  RespawnBtn = respawn.get();
  RespawnBtn->SetOnClick(
      [this]()
      {
        if (OnRespawn)
        {
          OnRespawn();
        }
      });
  Panel->AddChild(std::move(respawn));

  auto spectate = std::make_unique<UGuiButton>(Theme, "Spectate");
  SpectateBtn = spectate.get();
  SpectateBtn->SetOnClick(
      [this]()
      {
        if (OnSpectate)
        {
          OnSpectate();
        }
      });
  Panel->AddChild(std::move(spectate));

  Root = std::move(panel);
  SetVisible(false);
}

void UDeathScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (visible)
  {
    Relayout();
  }
}

void UDeathScreen::SetCause(const std::string &cause)
{
  if (CauseLabel)
  {
    CauseLabel->SetText(cause.empty() ? "Fatal wounds" : cause);
  }
}

void UDeathScreen::SetOnRespawn(std::function<void()> handler)
{
  OnRespawn = std::move(handler);
}

void UDeathScreen::SetOnSpectate(std::function<void()> handler)
{
  OnSpectate = std::move(handler);
}

void UDeathScreen::Relayout()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const int w = 360;
  const int h = 220;
  const int x = (ViewportW - w) / 2;
  const int y = (ViewportH - h) / 2;
  Panel->SetBounds({x, y, w, h});
  if (Title)
  {
    Title->SetBounds({x + 24, y + 24, w - 48, 32});
  }
  if (CauseLabel)
  {
    CauseLabel->SetBounds({x + 24, y + 64, w - 48, 24});
  }
  if (RespawnBtn)
  {
    RespawnBtn->SetBounds({x + 40, y + 110, w - 80, 36});
  }
  if (SpectateBtn)
  {
    SpectateBtn->SetBounds({x + 40, y + 156, w - 80, 36});
  }
}

} // namespace cutum
