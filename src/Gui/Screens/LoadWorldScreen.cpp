#include "Gui/Screens/LoadWorldScreen.h"

#include "Gui/Core/GuiContext.h"

#include "Gui/Core/GuiRenderer.h"

#include "Gui/Interfaces/IUGuiMenuHost.h"

#include "Gui/Widgets/GuiButton.h"

#include "Gui/Widgets/GuiDialogFrame.h"

#include "Gui/Widgets/GuiLabel.h"

#include "Gui/Widgets/GuiListView.h"

#include "Gui/Widgets/GuiPanel.h"

#include "Gui/Widgets/GuiWindow.h"

#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{

std::string FormatPackList(const std::vector<std::string> &packs)
{
  if (packs.empty())
  {
    return "Resource packs: (defaults)";
  }
  std::ostringstream oss;
  oss << "Resource packs: ";
  for (size_t i = 0; i < packs.size(); ++i)
  {
    if (i > 0)
    {
      oss << ", ";
    }
    oss << packs[i];
  }
  return oss.str();
}

void UpdatePackSubtitle(IUGuiMenuHost *host, UGuiLabel *label, int index)
{
  if (!host || !label || index < 0)
  {
    return;
  }
  const auto &names = host->GetWorldNames();
  if (index >= static_cast<int>(names.size()))
  {
    return;
  }
  label->SetText(FormatPackList(host->PeekWorldResourcePacks(names[index])));
}

} // namespace

ULoadWorldScreen::ULoadWorldScreen(IUGuiMenuHost *host)

    : Host(host)

{
}

void ULoadWorldScreen::OnLoad()

{

  if (!Host || !List)
  {

    return;
  }

  const int idx = List->GetSelectedIndex();

  if (idx < 0)
  {

    return;
  }

  const auto &names = Host->GetWorldNames();

  if (idx >= static_cast<int>(names.size()))
  {

    return;
  }

  const std::string worldName = names[static_cast<size_t>(idx)];

  Host->SaveIfNeededAndProceed([this, worldName]()
                               { Host->LoadSelectedWorld(worldName); });
}

void ULoadWorldScreen::Build(UGuiContext &ctx)

{

  int w = ctx.GetRenderer().GetWindowWidth();

  int h = ctx.GetRenderer().GetWindowHeight();

  if (w > 0 && h > 0)
  {

    ViewportW = w;

    ViewportH = h;
  }

  if (Host)
  {

    Host->RefreshWorldList();
  }

  const GuiTheme &theme = ctx.GetTheme();

  const auto &worlds =
      Host ? Host->GetWorldNames() : std::vector<std::string>{};

  auto backdrop = std::make_unique<UGuiPanel>(&theme);

  backdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int winW = std::min(480, ViewportW - 40);

  const int winH = std::min(400, ViewportH - 40);

  auto window = std::make_unique<UGuiWindow>(&theme, "Load World");

  Window = window.get();

  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);

  DialogFrame = frame.get();

  auto bodyPanel = std::make_unique<UGuiPanel>(&theme);
  BodyPanel = bodyPanel.get();

  auto list = std::make_unique<UGuiListView>(&theme);

  List = list.get();

  list->SetItems(worlds);

  if (!worlds.empty())
  {

    list->SetSelectedIndex(0);
  }

  bodyPanel->AddChild(std::move(list));

  auto packSubtitle = std::make_unique<UGuiLabel>(&theme, "");
  PackSubtitle = packSubtitle.get();
  packSubtitle->SetTextAlign(GuiTextAlign::Left);
  bodyPanel->AddChild(std::move(packSubtitle));
  if (Host && !worlds.empty())
  {
    UpdatePackSubtitle(Host, PackSubtitle, 0);
  }
  List->SetOnSelectionChanged(
      [this](int index) { UpdatePackSubtitle(Host, PackSubtitle, index); });

  frame->SetFixedBody(std::move(bodyPanel));

  auto empty = std::make_unique<UGuiLabel>(&theme, "No saved worlds.");

  EmptyLabel = empty.get();

  empty->SetVisible(worlds.empty());

  window->AddChild(std::move(empty));

  auto loadBtn = std::make_unique<UGuiButton>(&theme, "Load");

  LoadBtn = loadBtn.get();

  loadBtn->SetEnabled(!worlds.empty());
  loadBtn->SetOnClick([this]() { OnLoad(); });
  frame->AddFooterButton(std::move(loadBtn));

  frame
      ->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Cancel"))

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

void ULoadWorldScreen::OnViewportChanged(int width, int height)

{

  UGuiScreenBase::OnViewportChanged(width, height);

  Relayout();
}

void ULoadWorldScreen::Relayout()

{

  if (!Window || !DialogFrame)
  {

    return;
  }

  const int winW = std::min(480, ViewportW - 40);

  const int winH = std::min(400, ViewportH - 40);

  Window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  DialogFrame->SetBounds(Window->GetClientArea());

  DialogFrame->LayoutFrame();

  if (BodyPanel && List && PackSubtitle)
  {
    const GuiRect area = BodyPanel->GetBounds();
    constexpr int subH = 22;
    List->SetBounds({area.X, area.Y, area.W, std::max(0, area.H - subH - 4)});
    PackSubtitle->SetBounds(
        {area.X, area.Y + area.H - subH, area.W, subH});
  }

  if (EmptyLabel)
  {

    const GuiRect client = Window->GetClientArea();

    EmptyLabel->SetBounds({client.X + 16, client.Y + 24, client.W - 32, 24});
  }
}

} // namespace cutum
