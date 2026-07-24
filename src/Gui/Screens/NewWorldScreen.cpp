#include "Gui/Screens/NewWorldScreen.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Widgets/WorldViewSettingsForm.h"
#include "Gui/Widgets/ResourcePackPickerForm.h"
#include <algorithm>
#include <iostream>

namespace cutum
{

namespace
{

GuiGridSpec BuildWorldGridSpec(const GuiMetrics &metrics, int width)
{
  GuiGridSpec spec;
  spec.columns = width < metrics.Dp(680) ? 1 : 2;
  spec.hGap = metrics.Dp(12);
  spec.vGap = metrics.Dp(8);
  spec.Padding = metrics.Dp(4);
  spec.columnWeights = {1, 1};
  return spec;
}

std::pair<int, int> NewWorldWindowSize(const GuiTheme &theme, int viewportW,
                                       int viewportH)
{
  return {std::min(theme.DialogDefaultWidth, viewportW - theme.DialogMargin),
          std::min(theme.DialogDefaultHeight, viewportH - theme.DialogMargin)};
}

} // namespace

UNewWorldScreen::UNewWorldScreen(IUGuiMenuHost *host) : Host(host) {}

UNewWorldScreen::~UNewWorldScreen() = default;

void UNewWorldScreen::OnCreate()
{
  if (!Host || !WorldForm)
  {
    return;
  }
  const ProceduralSettings settings = WorldForm->ReadSettings();
  const WorldViewSettings view =
      ViewForm ? ViewForm->ReadSettings() : WorldViewSettings{};
  ResourcePackSelection packs =
      PackForm ? PackForm->ReadSelection() : ResourcePackSelection{};
  if (packs.Primary.empty())
  {
    packs = Host->GetDefaultResourcePackSelection();
  }
  if (packs.Primary.empty())
  {
    // Keep the dialog open but make the failure visible in logs/console.
    std::cerr << "NewWorld: Create ignored — no primary resource pack selected "
                 "and no default packs configured."
              << std::endl;
    return;
  }
  if (packs.WorldgenOwner.empty())
  {
    packs.WorldgenOwner = packs.Primary.front();
  }
  auto create = [this, settings, packs, view]()
  { Host->CreateNewWorldWithSettings(settings, packs, view); };
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

  const auto [winW, winH] = NewWorldWindowSize(theme, ViewportW, ViewportH);
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

  auto viewSection = std::make_unique<UGuiLabel>(&theme, "View:");
  ViewSectionLabel = viewSection.get();
  body->AddChild(std::move(viewSection));
  ViewForm = std::make_unique<UWorldViewSettingsForm>(&theme);
  ViewForm->SetSettings(WorldViewSettings{});
  ViewForm->SetOnLayoutChanged([this]() { RequestBodyRelayout(); });
  ViewForm->BuildInto(*body);

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
  const GuiTheme *theme = GetMetrics().Theme;
  if (!theme)
  {
    return;
  }
  const auto [winW, winH] = NewWorldWindowSize(*theme, ViewportW, ViewportH);
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
  if (width <= 0)
  {
    return 0;
  }
  const GuiRect area{0, 0, width, 100000};
  const GuiGridSpec spec = BuildWorldGridSpec(GetMetrics(), width);
  const GuiTheme *theme = GetMetrics().Theme;
  const int section_gap = Scaled(12);
  const int label_h = theme ? theme->TabBarHeight : 28;
  int height = 0;
  if (ViewForm && theme)
  {
    height += label_h + ViewForm->MeasureHeight(area) + section_gap;
  }
  if (WorldForm)
  {
    height += WorldForm->MeasureGridHeight(area, spec);
  }
  if (PackForm && theme)
  {
    height += section_gap + label_h + PackForm->MeasureHeight(area);
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
  const GuiTheme *theme = GetMetrics().Theme;
  const int content_pad = theme ? theme->ContentPad : 8;
  const GuiRect layoutArea{vp.X + content_pad, vp.Y + content_pad - scrollY,
                           std::max(0, vp.W - 2 * content_pad),
                           std::max(0, vp.H - 2 * content_pad)};
  const int contentH = MeasureWorldPageContentHeight(layoutArea.W);
  const int pageH = std::max(vp.H, contentH + 2 * content_pad);
  WorldPage->SetBounds({vp.X, vp.Y - scrollY, vp.W, pageH});
  LayoutWorldPage(layoutArea);
}

void UNewWorldScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (area.W <= 0)
  {
    return;
  }
  const GuiTheme *theme = GetMetrics().Theme;
  const int section_gap = Scaled(12);
  const int label_h = theme ? theme->TabBarHeight : 28;
  int y = area.Y;

  if (ViewForm)
  {
    if (ViewSectionLabel)
    {
      ViewSectionLabel->SetBounds({area.X, y, area.W, label_h});
      y += label_h;
    }
    const int viewH = ViewForm->MeasureHeight(area);
    ViewForm->Layout({area.X, y, area.W, viewH});
    y += viewH + section_gap;
  }

  if (WorldForm)
  {
    const GuiGridSpec spec = BuildWorldGridSpec(GetMetrics(), area.W);
    const int gridH = WorldForm->MeasureGridHeight(area, spec);
    WorldForm->LayoutGrid({area.X, y, area.W, gridH}, spec);
    y += gridH + section_gap;
  }

  if (!PackForm)
  {
    return;
  }
  if (PackSectionLabel)
  {
    PackSectionLabel->SetBounds({area.X, y, area.W, label_h});
    y += label_h;
  }
  PackForm->Layout({area.X, y, area.W, PackForm->MeasureHeight(area)});
}

} // namespace cutum
