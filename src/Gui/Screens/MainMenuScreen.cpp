#include "Gui/Screens/MainMenuScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiGameActions.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Version.h"

namespace cutum
{

namespace
{
constexpr int kQuitModalZOrder = 100;
}

UMainMenuScreen::UMainMenuScreen(IGuiGameActions *actions) : Actions(actions) {}

void UMainMenuScreen::ShowQuitConfirmation(bool visible)
{
  QuitDialogVisible = visible;
  if (QuitBackdrop)
  {
    QuitBackdrop->SetVisible(visible);
  }
  if (QuitDialog)
  {
    QuitDialog->SetVisible(visible);
  }
  if (QuitMessage)
  {
    QuitMessage->SetVisible(visible);
  }
  if (QuitYesButton)
  {
    QuitYesButton->SetVisible(visible);
  }
  if (QuitNoButton)
  {
    QuitNoButton->SetVisible(visible);
  }
  if (visible)
  {
    if (QuitBackdrop)
    {
      QuitBackdrop->SetZOrder(kQuitModalZOrder);
    }
    for (UGuiButton *btn : Buttons)
    {
      if (btn)
      {
        btn->SetEnabled(false);
      }
    }
    RelayoutQuitDialog();
  }
  else
  {
    if (QuitBackdrop)
    {
      QuitBackdrop->SetZOrder(0);
    }
    for (UGuiButton *btn : Buttons)
    {
      if (btn)
      {
        btn->SetEnabled(true);
      }
    }
  }
}

void UMainMenuScreen::Build(UGuiContext &ctx)
{
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0 && h > 0)
  {
    ViewportW = w;
    ViewportH = h;
  }
  const GuiTheme &theme = ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(&theme);
  panel->SetBounds({0, 0, ViewportW, ViewportH});

  auto title = std::make_unique<UGuiLabel>(&theme, "Cubatarium");
  title->SetTextAlign(GuiTextAlign::Center);
  title->SetBounds({0, 0, 400, 56});
  Title = title.get();

  const bool resume = Actions && Actions->HasPausedSession();
  auto primary = std::make_unique<UGuiButton>(
      &theme, resume ? "Resume" : "Load Last World");
  primary->SetOnClick(
      [this, resume]()
      {
        if (!Actions)
        {
          return;
        }
        if (resume)
        {
          Actions->ResumeGame();
        }
        else
        {
          Actions->LoadLastWorld();
        }
      });

  auto loadWorld = std::make_unique<UGuiButton>(&theme, "Load World");
  loadWorld->SetOnClick(
      [this]()
      {
        if (Actions)
        {
          Actions->OpenLoadWorld();
        }
      });

  auto newWorld = std::make_unique<UGuiButton>(&theme, "New World");
  newWorld->SetOnClick(
      [this]()
      {
        if (Actions)
        {
          Actions->OpenNewWorld();
        }
      });

  auto settings = std::make_unique<UGuiButton>(&theme, "Settings");
  settings->SetOnClick(
      [this]()
      {
        if (Actions)
        {
          Actions->OpenSettings();
        }
      });

  auto quit = std::make_unique<UGuiButton>(&theme, "Quit");
  quit->SetOnClick([this]() { ShowQuitConfirmation(true); });

  Buttons.clear();
  Buttons.push_back(primary.get());
  Buttons.push_back(loadWorld.get());
  Buttons.push_back(newWorld.get());
  Buttons.push_back(settings.get());
  Buttons.push_back(quit.get());

  auto version = std::make_unique<UGuiLabel>(&theme, kCubatariumVersion);
  version->SetTextAlign(GuiTextAlign::Left);
  version->SetUseSecondaryColor(true);
  VersionLabel = version.get();

  auto quitBackdrop = std::make_unique<UGuiPanel>(&theme);
  quitBackdrop->SetVisible(false);
  QuitBackdrop = quitBackdrop.get();

  auto quitDialog = std::make_unique<UGuiPanel>(&theme);
  quitDialog->SetVisible(false);
  QuitDialog = quitDialog.get();

  auto quitMessage =
      std::make_unique<UGuiLabel>(&theme, "Exit the application?");
  quitMessage->SetTextAlign(GuiTextAlign::Center);
  quitMessage->SetVisible(false);
  QuitMessage = quitMessage.get();

  auto quitYes = std::make_unique<UGuiButton>(&theme, "Yes");
  quitYes->SetVisible(false);
  quitYes->SetOnClick(
      [this]()
      {
        if (Actions)
        {
          Actions->QuitApplication();
        }
      });
  QuitYesButton = quitYes.get();

  auto quitNo = std::make_unique<UGuiButton>(&theme, "No");
  quitNo->SetVisible(false);
  quitNo->SetOnClick([this]() { ShowQuitConfirmation(false); });
  QuitNoButton = quitNo.get();

  quitDialog->AddChild(std::move(quitMessage));
  quitDialog->AddChild(std::move(quitYes));
  quitDialog->AddChild(std::move(quitNo));
  quitBackdrop->AddChild(std::move(quitDialog));

  panel->AddChild(std::move(title));
  panel->AddChild(std::move(version));
  panel->AddChild(std::move(primary));
  panel->AddChild(std::move(loadWorld));
  panel->AddChild(std::move(newWorld));
  panel->AddChild(std::move(settings));
  panel->AddChild(std::move(quit));
  panel->AddChild(std::move(quitBackdrop));
  Root = std::move(panel);
  Relayout();
}

void UMainMenuScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UMainMenuScreen::RelayoutQuitDialog()
{
  if (!QuitBackdrop || !QuitDialog || !QuitMessage || !QuitYesButton ||
      !QuitNoButton)
  {
    return;
  }
  QuitBackdrop->SetBounds({0, 0, ViewportW, ViewportH});

  constexpr int dialogW = 360;
  constexpr int dialogH = 160;
  const int dialogX = (ViewportW - dialogW) / 2;
  const int dialogY = (ViewportH - dialogH) / 2;
  QuitDialog->SetBounds({dialogX, dialogY, dialogW, dialogH});

  QuitMessage->SetBounds({dialogX + 16, dialogY + 16, dialogW - 32, 40});

  constexpr int btnW = 120;
  constexpr int btnH = 40;
  constexpr int btnGap = 16;
  const int btnY = dialogY + dialogH - btnH - 20;
  const int totalBtnW = btnW * 2 + btnGap;
  const int btnStartX = dialogX + (dialogW - totalBtnW) / 2;
  QuitYesButton->SetBounds({btnStartX, btnY, btnW, btnH});
  QuitNoButton->SetBounds({btnStartX + btnW + btnGap, btnY, btnW, btnH});
}

void UMainMenuScreen::Relayout()
{
  if (!Root)
  {
    return;
  }
  const GuiRect full{0, 0, ViewportW, ViewportH};
  Root->SetBounds(full);

  if (Title)
  {
    UGuiLayout::AnchorChild(full, GuiAnchorKind::TopCenter, 20, Title);
  }

  if (VersionLabel)
  {
    constexpr int margin = 8;
    constexpr int labelH = 24;
    constexpr int labelW = 360;
    VersionLabel->SetBounds(
        {margin, ViewportH - labelH - margin, labelW, labelH});
  }

  if (!Buttons.empty())
  {
    const int btnW = 300;
    const int btnH = 44;
    const int spacing = 12;
    const int stackH = static_cast<int>(Buttons.size()) * btnH +
                       static_cast<int>(Buttons.size() - 1) * spacing;
    GuiRect stackArea{(ViewportW - btnW) / 2, (ViewportH - stackH) / 2, btnW,
                      stackH};
    std::vector<UGuiWidget *> children;
    for (UGuiButton *btn : Buttons)
    {
      children.push_back(btn);
    }
    UGuiLayout::StackVertical(stackArea, spacing, 0, children);
  }

  if (QuitDialogVisible)
  {
    RelayoutQuitDialog();
  }
}

} // namespace cutum
