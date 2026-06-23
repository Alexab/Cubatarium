#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Widgets/ResourcePackPickerForm.h"
#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kNewWorldWinW = 1040;
constexpr int kNewWorldWinH = 720;
constexpr int kNewWorldMargin = 32;
constexpr int kContentPad = 8;

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

  auto scroll = std::make_unique<UGuiScrollView>(&theme);
  scroll->SetScrollbarMode(GuiScrollbarMode::Auto);
  BodyScroll = scroll.get();
  scroll->SetAfterScrollLayout(
      [this](UGuiScrollView &sv) { LayoutWorldPageInScroll(sv); });

  auto body = std::make_unique<UGuiPanel>(&theme);
  body->SetDrawBackground(false);
  WorldPage = body.get();

  WorldForm = std::make_unique<UWorldGenSettingsForm>(&theme);
  WorldForm->SetSettings(procSnap);
  WorldForm->SetForNewWorldDefaults();
  WorldForm->SetOnLayoutChanged([this]() { RequestBodyRelayout(); });
  WorldForm->BuildInto(*body);

  auto packSection = std::make_unique<UGuiLabel>(&theme, "Resource packs:");
  PackSectionLabel = packSection.get();
  body->AddChild(std::move(packSection));
  PackForm = std::make_unique<UResourcePackPickerForm>(&theme);
  PackForm->SetPacks(Host ? Host->ListInstalledResourcePacks()
                          : std::vector<InstalledPackInfo>{});
  PackForm->SetSelection(Host ? Host->GetDefaultResourcePackSelection()
                              : ResourcePackSelection{});
  PackForm->SetOnLayoutChanged([this]() { RequestBodyRelayout(); });
  PackForm->BuildInto(*body);

  scroll->Content().AddChild(std::move(body));
  frame->SetFixedBody(std::move(scroll));

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
  const int prevW = ViewportW;
  const int prevH = ViewportH;
  UGuiScreenBase::OnViewportChanged(width, height);
  if (ViewportW == prevW && ViewportH == prevH)
  {
    return;
  }
  Relayout();
}

void UNewWorldScreen::Update(double /*dt*/)
{
  if (NeedsBodyRelayout && BodyScroll)
  {
    NeedsBodyRelayout = false;
    BodyScroll->LayoutContent(0, 0);
  }
}

void UNewWorldScreen::RequestBodyRelayout()
{
  NeedsBodyRelayout = true;
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
  if (BodyScroll)
  {
    BodyScroll->SetScrollY(0);
    BodyScroll->LayoutContent(0, 0);
  }
}

int UNewWorldScreen::MeasureWorldPageContentHeight(int width) const
{
  if (!WorldForm || width <= 0)
  {
    return 0;
  }
  const GuiRect area{0, 0, width, 100000};
  const GuiGridSpec spec = BuildWorldGridSpec(width);
  int height = WorldForm->MeasureGridHeight(area, spec);
  if (PackForm)
  {
    constexpr int kSectionGap = 12;
    constexpr int kLabelH = 28;
    height += kSectionGap + kLabelH + PackForm->MeasureHeight(area);
  }
  return height;
}

void UNewWorldScreen::LayoutWorldPageInScroll(UGuiScrollView &scroll) const
{
  if (!WorldPage)
  {
    return;
  }
  const GuiRect vp = scroll.GetBounds();
  const int scrollY = scroll.GetScrollY();
  const GuiRect layoutArea{vp.X + kContentPad, vp.Y + kContentPad - scrollY,
                           std::max(0, vp.W - 2 * kContentPad),
                           std::max(0, vp.H - 2 * kContentPad)};
  const int contentH = MeasureWorldPageContentHeight(layoutArea.W);
  const int pageH = std::max(vp.H, contentH + 2 * kContentPad);
  WorldPage->SetBounds({vp.X, vp.Y - scrollY, vp.W, pageH});
  LayoutWorldPage(layoutArea);
}

void UNewWorldScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!WorldForm || area.W <= 0)
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
  constexpr int kSectionGap = 12;
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
