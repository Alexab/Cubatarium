#include "Gui/Screens/LoadWorldScreen.h"

#include "Gui/Core/GuiContext.h"

#include "Gui/Core/GuiRenderer.h"

#include "Gui/Interfaces/IGuiMenuHost.h"

#include "Gui/Widgets/GuiButton.h"

#include "Gui/Widgets/GuiDialogFrame.h"

#include "Gui/Widgets/GuiLabel.h"

#include "Gui/Widgets/GuiListView.h"

#include "Gui/Widgets/GuiPanel.h"

#include "Gui/Widgets/GuiWindow.h"

#include <algorithm>

namespace cutum
{

ULoadWorldScreen::ULoadWorldScreen(IGuiMenuHost *host)

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

  auto list = std::make_unique<UGuiListView>(&theme);

  List = list.get();

  list->SetItems(worlds);

  if (!worlds.empty())
  {

    list->SetSelectedIndex(0);
  }

  frame->SetFixedBody(std::move(list));

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

  if (EmptyLabel)
  {

    const GuiRect client = Window->GetClientArea();

    EmptyLabel->SetBounds({client.X + 16, client.Y + 24, client.W - 32, 24});
  }
}

} // namespace cutum
