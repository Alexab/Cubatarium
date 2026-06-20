#include "Gui/Screens/WorldResourcePacksScreen.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/ResourcePackPickerForm.h"
#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kWinW = 920;
constexpr int kWinH = 620;
constexpr int kMargin = 32;
constexpr int kContentPad = 8;

} // namespace

UWorldResourcePacksScreen::UWorldResourcePacksScreen(
    IGuiMenuHost *host, std::function<void()> onClose)
    : Host(host), OnClose(std::move(onClose))
{
}

void UWorldResourcePacksScreen::OnApply()
{
  if (!Host || !PackForm)
  {
    return;
  }
  ResourcePackSelection selection = PackForm->ReadSelection();
  if (selection.Primary.empty())
  {
    return;
  }
  if (selection.WorldgenOwner.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  Host->ApplyResourcePacksToCurrentWorld(selection);
  if (OnClose)
  {
    OnClose();
  }
}

void UWorldResourcePacksScreen::Build(UGuiContext &ctx)
{
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0 && h > 0)
  {
    ViewportW = w;
    ViewportH = h;
  }

  const GuiTheme &theme = ctx.GetTheme();
  auto backdrop = std::make_unique<UGuiPanel>(&theme);
  backdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int winW = std::min(kWinW, ViewportW - kMargin);
  const int winH = std::min(kWinH, ViewportH - kMargin);
  auto window = std::make_unique<UGuiWindow>(&theme, "World settings");
  Window = window.get();
  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  DialogFrame = frame.get();

  auto scroll = std::make_unique<UGuiScrollView>(&theme);
  BodyScroll = scroll.get();
  scroll->SetAfterScrollLayout(
      [this](UGuiScrollView &sv) { LayoutBody(sv); });

  auto body = std::make_unique<UGuiPanel>(&theme);
  body->SetDrawBackground(false);
  BodyPanel = body.get();

  auto warn = std::make_unique<UGuiLabel>(
      &theme,
      "Changing packs may alter block textures. Save the world after applying.");
  WarningLabel = warn.get();
  body->AddChild(std::move(warn));

  PackForm = std::make_unique<UResourcePackPickerForm>(&theme);
  PackForm->SetPacks(Host ? Host->ListInstalledResourcePacks()
                          : std::vector<InstalledPackInfo>{});
  PackForm->SetSelection(Host ? Host->GetCurrentWorldResourcePackSelection()
                              : ResourcePackSelection{});
  PackForm->BuildInto(*body);

  scroll->Content().AddChild(std::move(body));
  frame->SetFixedBody(std::move(scroll));

  frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Apply"))
      .SetOnClick([this]() { OnApply(); });
  frame->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Cancel"))
      .SetOnClick([this]() {
        if (OnClose)
        {
          OnClose();
        }
      });

  window->AddChild(std::move(frame));
  backdrop->AddChild(std::move(window));
  Root = std::move(backdrop);
  Relayout();
}

void UWorldResourcePacksScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UWorldResourcePacksScreen::Update(double /*dt*/)
{
  if (BodyScroll)
  {
    BodyScroll->LayoutContent(0, 0);
  }
}

void UWorldResourcePacksScreen::Relayout()
{
  if (!Window || !DialogFrame)
  {
    return;
  }
  const int winW = std::min(kWinW, ViewportW - kMargin);
  const int winH = std::min(kWinH, ViewportH - kMargin);
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

int UWorldResourcePacksScreen::MeasureBodyHeight(int width) const
{
  constexpr int kWarnH = 48;
  constexpr int kGap = 12;
  int h = kWarnH + kGap;
  if (PackForm)
  {
    h += PackForm->MeasureHeight({0, 0, width, 100000});
  }
  return h;
}

void UWorldResourcePacksScreen::LayoutBody(UGuiScrollView &scroll) const
{
  if (!BodyPanel)
  {
    return;
  }
  const GuiRect vp = scroll.GetBounds();
  const GuiRect layoutArea{vp.X + kContentPad, vp.Y + kContentPad,
                           std::max(0, vp.W - 2 * kContentPad),
                           std::max(0, vp.H - 2 * kContentPad)};
  const int contentH = MeasureBodyHeight(layoutArea.W);
  const int pageH = std::max(vp.H, contentH + 2 * kContentPad);
  BodyPanel->SetBounds({vp.X, vp.Y - scroll.GetScrollY(), vp.W, pageH});

  constexpr int kWarnH = 48;
  constexpr int kGap = 12;
  int y = layoutArea.Y;
  if (WarningLabel)
  {
    WarningLabel->SetBounds({layoutArea.X, y, layoutArea.W, kWarnH});
    y += kWarnH + kGap;
  }
  if (PackForm)
  {
    PackForm->Layout(
        {layoutArea.X, y, layoutArea.W, PackForm->MeasureHeight(layoutArea)});
  }
}

} // namespace cutum
