#include "Gui/Screens/MainMenuScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IUGuiGameActions.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Version.h"
#include <type_traits>
#include <typeinfo>

namespace cutum
{

namespace
{
constexpr int kQuitModalZOrder = 100;
}

UMainMenuScreen::UMainMenuScreen(IUGuiGameActions *actions) : Actions(actions) {}

struct UMainMenuScreen::PrimaryAction
{
  UMainMenuScreen *owner;
  bool resume;
  void operator()() const
  {
    if (auto *actions = owner->Actions)
    {
      if (resume)
        actions->ResumeGame();
      else
        actions->LoadLastWorld();
    }
  }
};

template<void (IUGuiGameActions::*Invoke)()> struct UMainMenuScreen::MenuAction
{
  UMainMenuScreen *owner;
  void operator()() const
  {
    if (auto *actions = owner->Actions)
      (actions->*Invoke)();
  }
};

template<bool Visible> struct UMainMenuScreen::QuitDialogAction
{
  UMainMenuScreen *owner;
  void operator()() const { owner->ShowQuitConfirmation(Visible); }
};

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
  auto *title_label = title.get();

  const bool resume = Actions && Actions->HasPausedSession();
  auto primary = std::make_unique<UGuiButton>(
      &theme, resume ? "Resume" : "Load Last World");
  primary->SetOnClick(PrimaryAction{this, resume});

  auto loadWorld = std::make_unique<UGuiButton>(&theme, "Load World");
  loadWorld->SetOnClick(MenuAction<&IUGuiGameActions::OpenLoadWorld>{this});

  std::unique_ptr<UGuiButton> worldSettings;
  if (resume)
  {
    worldSettings = std::make_unique<UGuiButton>(&theme, "World settings");
    worldSettings->SetOnClick(MenuAction<&IUGuiGameActions::OpenWorldSettings>{this});
  }

  auto newWorld = std::make_unique<UGuiButton>(&theme, "New World");
  newWorld->SetOnClick(MenuAction<&IUGuiGameActions::OpenNewWorld>{this});

  auto settings = std::make_unique<UGuiButton>(&theme, "Settings");
  settings->SetOnClick(MenuAction<&IUGuiGameActions::OpenSettings>{this});

  auto quit = std::make_unique<UGuiButton>(&theme, "Quit");
  quit->SetOnClick(QuitDialogAction<true>{this});

  std::vector<UGuiButton *> buttons;
  buttons.reserve(resume ? 6 : 5);
  buttons.push_back(primary.get());
  if (worldSettings)
  {
    buttons.push_back(worldSettings.get());
  }
  buttons.push_back(loadWorld.get());
  buttons.push_back(newWorld.get());
  buttons.push_back(settings.get());
  buttons.push_back(quit.get());

  auto version = std::make_unique<UGuiLabel>(&theme, kCubatariumVersion);
  version->SetTextAlign(GuiTextAlign::Left);
  version->SetInfoBadgeStyle(true);
  auto *version_label = version.get();

  auto quitBackdrop = std::make_unique<UGuiPanel>(&theme);
  quitBackdrop->SetVisible(false);
  auto *backdrop_panel = quitBackdrop.get();

  auto quitDialog = std::make_unique<UGuiPanel>(&theme);
  quitDialog->SetVisible(false);
  auto *dialog_panel = quitDialog.get();

  auto quitMessage =
      std::make_unique<UGuiLabel>(&theme, "Exit the application?");
  quitMessage->SetTextAlign(GuiTextAlign::Center);
  quitMessage->SetVisible(false);
  auto *quit_label = quitMessage.get();

  auto quitYes = std::make_unique<UGuiButton>(&theme, "Yes");
  quitYes->SetVisible(false);
  quitYes->SetOnClick(MenuAction<&IUGuiGameActions::QuitApplication>{this});
  auto *yes_button = quitYes.get();

  auto quitNo = std::make_unique<UGuiButton>(&theme, "No");
  quitNo->SetVisible(false);
  quitNo->SetOnClick(QuitDialogAction<false>{this});
  auto *no_button = quitNo.get();

  quitDialog->AddChild(std::move(quitMessage));
  quitDialog->AddChild(std::move(quitYes));
  quitDialog->AddChild(std::move(quitNo));
  quitBackdrop->AddChild(std::move(quitDialog));

  panel->AddChild(std::move(title));
  panel->AddChild(std::move(version));
  panel->AddChild(std::move(primary));
  if (worldSettings)
  {
    panel->AddChild(std::move(worldSettings));
  }
  panel->AddChild(std::move(loadWorld));
  panel->AddChild(std::move(newWorld));
  panel->AddChild(std::move(settings));
  panel->AddChild(std::move(quit));
  panel->AddChild(std::move(quitBackdrop));
  Root = std::move(panel);
  Buttons = std::move(buttons);
  Title = title_label;
  VersionLabel = version_label;
  QuitBackdrop = backdrop_panel;
  QuitDialog = dialog_panel;
  QuitMessage = quit_label;
  QuitYesButton = yes_button;
  QuitNoButton = no_button;
  ShowQuitConfirmation(QuitDialogVisible);
  Relayout();
}

MainMenuStorage UMainMenuScreen::InspectOwnedStorage(const UGuiContext &context,
                                                    const IUGuiGameActions *actions) const noexcept
{
  MainMenuStorage out;
  out.present = true;
  if (typeid(*this) != typeid(UMainMenuScreen) || Context != &context || context.GetScreen() != this ||
      !actions || Actions != actions || !Root || typeid(*Root) != typeid(UGuiPanel) ||
      (Buttons.size() != 5 && Buttons.size() != 6))
    return out;

  StlSourceStorage objects, slots, text, callbacks, total;
  const auto *theme = &context.GetTheme();
  const auto node = [&](const auto &child, const auto *owner, bool leaf) {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(owner)>>;
    if (!child || child.get() != owner || typeid(*child) != typeid(Widget) ||
        owner->Theme != theme || (leaf && !child->GetChildren().empty()))
      return false;
    ++out.widgets;
    objects.Add(sizeof(Widget));
    slots.Buffer(child->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    if constexpr (std::is_same_v<Widget, UGuiLabel>)
      text.Text(owner->GetText());
    else if constexpr (std::is_same_v<Widget, UGuiButton>)
      text.Text(owner->GetLabel());
    return true;
  };
  const auto &children = Root->GetChildren();
  if (children.size() != Buttons.size() + 3 ||
      !node(Root, static_cast<const UGuiPanel *>(Root.get()), false) ||
      !node(children[0], Title, true) || !node(children[1], VersionLabel, true) ||
      !node(children.back(), QuitBackdrop, false))
    return out;
  for (size_t index = 0; index < Buttons.size(); ++index)
    if (!node(children[index + 2], Buttons[index], true) ||
        Buttons[index]->IsEnabled() == QuitDialogVisible)
      return out;
  const auto &backdrop = QuitBackdrop->GetChildren();
  if (backdrop.size() != 1 || !node(backdrop[0], QuitDialog, false))
    return out;
  const auto &dialog = QuitDialog->GetChildren();
  if (dialog.size() != 3 || !node(dialog[0], QuitMessage, true) ||
      !node(dialog[1], QuitYesButton, true) || !node(dialog[2], QuitNoButton, true))
    return out;
  for (const auto *widget : {static_cast<const UGuiWidget *>(QuitBackdrop),
                             static_cast<const UGuiWidget *>(QuitDialog),
                             static_cast<const UGuiWidget *>(QuitMessage),
                             static_cast<const UGuiWidget *>(QuitYesButton),
                             static_cast<const UGuiWidget *>(QuitNoButton)})
    if (widget->IsVisible() != QuitDialogVisible)
      return out;

  out.bindings_match = true;
  out.resume = Buttons.size() == 6;
  out.quit_dialog = QuitDialogVisible;
  const auto callback = [&](auto action, const UGuiButton &button) {
    using Action = decltype(action);
    const auto storage = InspectKnownOwnerCallback<Action>(button.OnClick, this,
                                                            StlSourceStorage::SupportedProfile());
    callbacks.Add(storage.requests);
    out.callbacks += storage.present;
    return storage.supported && storage.present;
  };
  const auto &primary = *Buttons[0];
  const auto *primary_action = primary.OnClick.target<PrimaryAction>();
  bool known = callback(PrimaryAction{nullptr, false}, primary) &&
               primary_action && primary_action->resume == out.resume;
  if (out.resume)
    known &= callback(MenuAction<&IUGuiGameActions::OpenWorldSettings>{nullptr}, *Buttons[1]);
  const size_t load = out.resume ? 2 : 1;
  known &= callback(MenuAction<&IUGuiGameActions::OpenLoadWorld>{nullptr}, *Buttons[load]);
  known &= callback(MenuAction<&IUGuiGameActions::OpenNewWorld>{nullptr}, *Buttons[load + 1]);
  known &= callback(MenuAction<&IUGuiGameActions::OpenSettings>{nullptr}, *Buttons[load + 2]);
  known &= callback(QuitDialogAction<true>{nullptr}, *Buttons.back());
  known &= callback(MenuAction<&IUGuiGameActions::QuitApplication>{nullptr}, *QuitYesButton);
  known &= callback(QuitDialogAction<false>{nullptr}, *QuitNoButton);

  StlSourceStorage button_index;
  button_index.Buffer(Buttons.capacity(), sizeof(UGuiButton *));
  out.screen_bytes = sizeof(*this);
  out.widget_bytes = objects.bytes.requested;
  out.children_bytes = slots.bytes.requested;
  out.button_index_bytes = button_index.bytes.requested;
  out.text_bytes = text.bytes.requested;
  out.callback_bytes = callbacks.bytes.requested;
  for (const auto bytes : {out.screen_bytes, out.widget_bytes, out.children_bytes,
                           out.button_index_bytes, out.text_bytes, out.callback_bytes})
    total.Add(bytes);
  out.requested = total.bytes.requested;
  out.supported = known && objects.bytes.supported && slots.bytes.supported &&
                  button_index.bytes.supported && text.bytes.supported && callbacks.bytes.supported &&
                  total.bytes.supported;
  return out;
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
  const GuiTheme *theme = GetMetrics().Theme;
  if (!theme)
  {
    return;
  }
  QuitBackdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int dialogW = Scaled(360);
  const int dialogH = Scaled(160);
  const int dialogX = (ViewportW - dialogW) / 2;
  const int dialogY = (ViewportH - dialogH) / 2;
  QuitDialog->SetBounds({dialogX, dialogY, dialogW, dialogH});

  QuitMessage->SetBounds(
      {dialogX + theme->Padding * 2, dialogY + theme->Padding * 2,
       dialogW - theme->Padding * 4, Scaled(40)});

  const int btnW = Scaled(120);
  const int btnH = Scaled(40);
  const int btnGap = Scaled(16);
  const int btnY = dialogY + dialogH - btnH - Scaled(20);
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
  const GuiTheme *theme = GetMetrics().Theme;
  if (!theme)
  {
    return;
  }
  const GuiRect full{0, 0, ViewportW, ViewportH};
  Root->SetBounds(full);

  if (Title)
  {
    UGuiLayout::AnchorChild(full, GuiAnchorKind::TopCenter, Scaled(20), Title);
  }

  if (VersionLabel)
  {
    const int margin = theme->Padding;
    const int labelH = theme->FontSizeBody + theme->Padding;
    const int labelW = Scaled(360);
    VersionLabel->SetBounds(
        {margin, ViewportH - labelH - margin, labelW, labelH});
  }

  if (!Buttons.empty())
  {
    const int btnW = theme->MenuButtonWidth;
    const int spacing = theme->MenuButtonSpacing;
    int stackH = 0;
    for (UGuiButton *btn : Buttons)
    {
      if (btn)
      {
        stackH += btn->GetPreferredHeight();
      }
    }
    stackH += static_cast<int>(Buttons.size() - 1) * spacing;
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
