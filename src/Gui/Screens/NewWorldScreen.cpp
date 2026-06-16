#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include <algorithm>

namespace cutum
{

namespace
{

GuiGridSpec BuildWorldGridSpec(int width)
{
  GuiGridSpec spec;
  if (width < 680)
  {
    spec.columns = 1;
  }
  else
  {
    spec.columns = 2;
  }
  spec.hGap = 12;
  spec.vGap = 8;
  spec.Padding = 4;
  spec.columnWeights = {1, 1};
  return spec;
}

} // namespace

UNewWorldScreen::UNewWorldScreen(IGuiMenuHost *host) : Host(host) {}

UNewWorldScreen::~UNewWorldScreen() = default;

void UNewWorldScreen::OnCreate()
{
  if (!Host || !WorldForm)
  {
    return;
  }
  const ProceduralSettings settings = WorldForm->ReadSettings();
  auto create = [this, settings]()
  { Host->CreateNewWorldWithSettings(settings); };
  Host->SaveIfNeededAndProceed(create);
}

void UNewWorldScreen::Build(UGuiContext &ctx)
{
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0 && h > 0)
  {
    ViewportW = w;
    ViewportH = h;
  }

  const GuiTheme &theme = ctx.GetTheme();
  const ProceduralSettings procSnap =
      Host ? Host->LoadProceduralTemplate() : ProceduralSettings{};

  auto backdrop = std::make_unique<UGuiPanel>(&theme);
  backdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int winW = std::min(820, ViewportW - 32);
  const int winH = std::min(500, ViewportH - 32);
  auto window = std::make_unique<UGuiWindow>(&theme, "New World");
  Window = window.get();
  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  DialogFrame = frame.get();
  frame->SetScrollbarMode(GuiScrollbarMode::Hidden);
  UGuiPanel &body = frame->AddScrollPage();
  WorldPage = &body;
  WorldForm = std::make_unique<UWorldGenSettingsForm>(&theme);
  WorldForm->SetSettings(procSnap);
  WorldForm->BuildInto(body);
  frame->SetScrollPageLayout(
      0, [this](const GuiRect &area) { return MeasureWorldPageHeight(area); },
      [this](const GuiRect &area) { LayoutWorldPage(area); });

  frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Create"))
      .SetOnClick([this]() { OnCreate(); });
  frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Cancel"))
      .SetOnClick(
          [this]()
          {
            if (Host)
            {
              Host->ReturnToMainMenu();
            }
          });

  window->AddChild(std::move(frame));
  backdrop->AddChild(std::move(window));
  Root = std::move(backdrop);
  Relayout();
}

void UNewWorldScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UNewWorldScreen::Relayout()
{
  if (!Window || !DialogFrame)
  {
    return;
  }
  const int winW = std::min(820, ViewportW - 32);
  const int winH = std::min(500, ViewportH - 32);
  Window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});
  DialogFrame->SetBounds(Window->GetClientArea());
  DialogFrame->LayoutFrame();
}

int UNewWorldScreen::MeasureWorldPageHeight(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return 0;
  }
  const GuiGridSpec spec = BuildWorldGridSpec(area.W);
  return WorldForm->MeasureGridHeight(area, spec);
}

void UNewWorldScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return;
  }
  const GuiGridSpec spec = BuildWorldGridSpec(area.W);
  WorldForm->LayoutGrid(area, spec);
}

} // namespace cutum
