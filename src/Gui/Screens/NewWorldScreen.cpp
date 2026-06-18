#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Widgets/ResourcePackPickerForm.h"
#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kNewWorldWinW = 860;
constexpr int kNewWorldWinH = 660;
constexpr int kNewWorldMargin = 32;

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

std::pair<int, int> NewWorldWindowSize(int viewportW, int viewportH)
{
  return {std::min(kNewWorldWinW, viewportW - kNewWorldMargin),
          std::min(kNewWorldWinH, viewportH - kNewWorldMargin)};
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
  ResourcePackSelection packs =
      PackForm ? PackForm->ReadSelection() : ResourcePackSelection{};
  if (packs.Primary.empty() && Host)
  {
    packs = Host->GetDefaultResourcePackSelection();
  }
  if (packs.Primary.empty())
  {
    return;
  }
  if (packs.WorldgenOwner.empty())
  {
    packs.WorldgenOwner = packs.Primary.front();
  }
  auto create = [this, settings, packs]()
  { Host->CreateNewWorldWithSettings(settings, packs); };
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

  const auto [winW, winH] = NewWorldWindowSize(ViewportW, ViewportH);
  auto window = std::make_unique<UGuiWindow>(&theme, "New World");
  Window = window.get();
  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  DialogFrame = frame.get();
  frame->SetScrollbarMode(GuiScrollbarMode::Auto);
  UGuiPanel &body = frame->AddScrollPage();
  WorldPage = &body;
  WorldForm = std::make_unique<UWorldGenSettingsForm>(&theme);
  WorldForm->SetSettings(procSnap);
  WorldForm->BuildInto(body);

  auto packSection = std::make_unique<UGuiLabel>(&theme, "Resource packs:");
  PackSectionLabel = packSection.get();
  body.AddChild(std::move(packSection));
  PackForm = std::make_unique<UResourcePackPickerForm>(&theme);
  PackForm->SetPacks(Host ? Host->ListInstalledResourcePacks()
                          : std::vector<InstalledPackInfo>{});
  PackForm->SetSelection(Host ? Host->GetDefaultResourcePackSelection()
                              : ResourcePackSelection{});
  PackForm->BuildInto(body);

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
  const auto [winW, winH] = NewWorldWindowSize(ViewportW, ViewportH);
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
  int height = WorldForm->MeasureGridHeight(area, spec);
  if (PackForm)
  {
    constexpr int kSectionGap = 16;
    constexpr int kLabelH = 28;
    height += kSectionGap + kLabelH + PackForm->MeasureHeight(area);
  }
  return height;
}

void UNewWorldScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return;
  }
  const GuiGridSpec spec = BuildWorldGridSpec(area.W);
  const int gridH = WorldForm->MeasureGridHeight(area, spec);
  WorldForm->LayoutGrid({area.X, area.Y, area.W, gridH}, spec);
  if (!PackForm)
  {
    return;
  }
  constexpr int kSectionGap = 16;
  constexpr int kLabelH = 28;
  int y = area.Y + gridH + kSectionGap;
  if (PackSectionLabel)
  {
    PackSectionLabel->SetBounds({area.X, y, area.W, kLabelH});
    y += kLabelH;
  }
  PackForm->Layout({area.X, y, area.W, PackForm->MeasureHeight(area)});
}

} // namespace cutum
