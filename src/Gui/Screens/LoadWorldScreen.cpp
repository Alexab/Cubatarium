#include "LoadWorldScreen.h"

#include "Gui/GuiContext.h"

#include "Gui/GuiRenderer.h"

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

    : host_(host)

{
}

void ULoadWorldScreen::OnLoad()

{

  if (!host_ || !list_)
  {

    return;
  }

  const int idx = list_->GetSelectedIndex();

  if (idx < 0)
  {

    return;
  }

  const auto &names = host_->GetWorldNames();

  if (idx >= static_cast<int>(names.size()))
  {

    return;
  }

  const std::string worldName = names[static_cast<size_t>(idx)];

  host_->SaveIfNeededAndProceed([this, worldName]()
                                { host_->LoadSelectedWorld(worldName); });
}

void ULoadWorldScreen::Build(UGuiContext &ctx)

{

  int w = ctx.GetRenderer().GetWindowWidth();

  int h = ctx.GetRenderer().GetWindowHeight();

  if (w > 0 && h > 0)
  {

    viewportW_ = w;

    viewportH_ = h;
  }

  if (host_)
  {

    host_->RefreshWorldList();
  }

  const GuiTheme &theme = ctx.GetTheme();

  const auto &worlds =
      host_ ? host_->GetWorldNames() : std::vector<std::string>{};

  auto backdrop = std::make_unique<UGuiPanel>(&theme);

  backdrop->SetBounds({0, 0, viewportW_, viewportH_});

  const int winW = std::min(480, viewportW_ - 40);

  const int winH = std::min(400, viewportH_ - 40);

  auto window = std::make_unique<UGuiWindow>(&theme, "Load World");

  Window = window.get();

  window->SetBounds(
      {(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);

  dialogFrame_ = frame.get();

  auto list = std::make_unique<UGuiListView>(&theme);

  list_ = list.get();

  list->SetItems(worlds);

  if (!worlds.empty())
  {

    list->SetSelectedIndex(0);
  }

  frame->SetFixedBody(std::move(list));

  auto empty = std::make_unique<UGuiLabel>(&theme, "No saved worlds.");

  emptyLabel_ = empty.get();

  empty->SetVisible(worlds.empty());

  window->AddChild(std::move(empty));

  auto loadBtn = std::make_unique<UGuiButton>(&theme, "Load");

  loadBtn_ = loadBtn.get();

  loadBtn->SetEnabled(!worlds.empty());
  loadBtn->SetOnClick([this]() { OnLoad(); });
  frame->AddFooterButton(std::move(loadBtn));

  frame
      ->AddFooterButton(std::make_unique<UGuiButton>(&theme, "Cancel"))

      .SetOnClick(
          [this]()
          {
            if (host_)
            {

              host_->ReturnToMainMenu();
            }
          });

  window->AddChild(std::move(frame));

  backdrop->AddChild(std::move(window));

  root_ = std::move(backdrop);

  Relayout();
}

void ULoadWorldScreen::OnViewportChanged(int width, int height)

{

  UGuiScreenBase::OnViewportChanged(width, height);

  Relayout();
}

void ULoadWorldScreen::Relayout()

{

  if (!Window || !dialogFrame_)
  {

    return;
  }

  const int winW = std::min(480, viewportW_ - 40);

  const int winH = std::min(400, viewportH_ - 40);

  Window->SetBounds(
      {(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

  dialogFrame_->SetBounds(Window->GetClientArea());

  dialogFrame_->LayoutFrame();

  if (emptyLabel_)
  {

    const GuiRect client = Window->GetClientArea();

    emptyLabel_->SetBounds({client.x + 16, client.y + 24, client.w - 32, 24});
  }
}

} // namespace cutum
