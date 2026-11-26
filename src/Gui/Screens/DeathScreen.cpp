#include "Gui/Screens/DeathScreen.h"
#include "App/Application.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include <type_traits>
#include <typeinfo>

namespace cutum
{

template<void (UApplication::*Action)()>
struct UDeathScreen::ButtonAction
{
  UApplication *owner;
  void operator()() const { (owner->*Action)(); }
};

UDeathScreen::UDeathScreen(UApplication &application) : Application(application) {}

void UDeathScreen::Build(UGuiContext &ctx)
{
  const auto *theme = &ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(50);
  panel->SetVisible(Visible);
  panel->ReserveChildren(5);

  auto title = std::make_unique<UGuiLabel>(theme, Hardcore ? "Hardcore World Lost" : "You Died");
  auto cause = std::make_unique<UGuiLabel>(theme, CauseLabel ? CauseLabel->GetText() : std::string{});
  cause->SetUseSecondaryColor(true);
  auto respawn = std::make_unique<UGuiButton>(theme, "Respawn");
  respawn->SetVisible(!Hardcore);
  respawn->SetOnClick(ButtonAction<&UApplication::RespawnPlayerFromDeathScreen>{&Application});
  auto spectate = std::make_unique<UGuiButton>(theme, "Spectate");
  spectate->SetOnClick(ButtonAction<&UApplication::SpectateFromDeathScreen>{&Application});
  auto returnToMenu = std::make_unique<UGuiButton>(theme, "Return to Menu");
  returnToMenu->SetVisible(Hardcore);
  returnToMenu->SetOnClick(ButtonAction<&UApplication::ScheduleReturnFromDeathScreen>{&Application});

  auto *nextTitle = static_cast<UGuiLabel *>(panel->AddChild(std::move(title)));
  auto *nextCause = static_cast<UGuiLabel *>(panel->AddChild(std::move(cause)));
  auto *nextRespawn = static_cast<UGuiButton *>(panel->AddChild(std::move(respawn)));
  auto *nextSpectate = static_cast<UGuiButton *>(panel->AddChild(std::move(spectate)));
  auto *nextMenu = static_cast<UGuiButton *>(panel->AddChild(std::move(returnToMenu)));
  Panel = panel.get();
  Root = std::move(panel);
  Title = nextTitle;
  CauseLabel = nextCause;
  RespawnBtn = nextRespawn;
  SpectateBtn = nextSpectate;
  ReturnToMenuBtn = nextMenu;
  Relayout();
}

DeathScreenStorage UDeathScreen::InspectOwnedStorage(const UGuiContext &context,
                                                     const UApplication &application) const noexcept
{
  DeathScreenStorage out;
  out.present = true;
  out.visible = Visible;
  out.hardcore = Hardcore;
  if (typeid(*this) != typeid(UDeathScreen) || &Application != &application ||
      Context != &context || !Root || Root.get() != Panel ||
      typeid(*Root) != typeid(UGuiPanel) || Panel->Theme != &context.GetTheme())
    return out;

  const auto &children = Panel->GetChildren();
  if (children.size() != 5)
    return out;
  const auto *theme = &context.GetTheme();
  const auto matches = [theme](const auto &child, const auto *alias) {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(alias)>>;
    return child && child.get() == alias && typeid(*child) == typeid(Widget) &&
           child->GetChildren().empty() && alias->Theme == theme;
  };
  out.bindings_match = Visible == Panel->IsVisible() &&
      matches(children[0], Title) && matches(children[1], CauseLabel) &&
      matches(children[2], RespawnBtn) && matches(children[3], SpectateBtn) &&
      matches(children[4], ReturnToMenuBtn) &&
      RespawnBtn->IsVisible() == !Hardcore && SpectateBtn->IsVisible() &&
      ReturnToMenuBtn->IsVisible() == Hardcore;
  if (!out.bindings_match)
    return out;

  out.screen_bytes = sizeof(*this);
  out.widgets = 1 + children.size();
  out.widget_bytes = sizeof(UGuiPanel) + 2 * sizeof(UGuiLabel) + 3 * sizeof(UGuiButton);
  StlSourceStorage slots, text, total;
  slots.Buffer(children.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto &child : children)
    slots.Buffer(child->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto *label : {Title, CauseLabel})
    text.Text(label->GetText());
  for (const auto *button : {RespawnBtn, SpectateBtn, ReturnToMenuBtn})
    text.Text(button->GetLabel());

  const bool profile = StlSourceStorage::SupportedProfile();
  const auto respawn = InspectKnownOwnerCallback<ButtonAction<&UApplication::RespawnPlayerFromDeathScreen>>(
      RespawnBtn->OnClick, &application, profile);
  const auto spectate = InspectKnownOwnerCallback<ButtonAction<&UApplication::SpectateFromDeathScreen>>(
      SpectateBtn->OnClick, &application, profile);
  const auto menu = InspectKnownOwnerCallback<ButtonAction<&UApplication::ScheduleReturnFromDeathScreen>>(
      ReturnToMenuBtn->OnClick, &application, profile);
  out.callback_present = {respawn.present, spectate.present, menu.present};
  out.callback_supported = {respawn.supported && respawn.present, spectate.supported && spectate.present,
                            menu.supported && menu.present};
  out.callback_bytes = respawn.requests + spectate.requests + menu.requests;
  out.children_bytes = slots.bytes.requested;
  out.text_bytes = text.bytes.requested;
  for (const auto amount : {out.screen_bytes, out.widget_bytes, out.children_bytes,
                            out.text_bytes, out.callback_bytes})
    total.Add(amount);
  out.requested = total.bytes.requested;
  out.supported = slots.bytes.supported && text.bytes.supported && total.bytes.supported &&
                  out.callback_supported[0] && out.callback_supported[1] && out.callback_supported[2];
  return out;
}

void UDeathScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UDeathScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
}

void UDeathScreen::SetHardcore(bool hardcore)
{
  if (Hardcore == hardcore)
    return;
  if (Title)
    Title->SetText(hardcore ? "Hardcore World Lost" : "You Died");
  Hardcore = hardcore;
  if (RespawnBtn)
  {
    RespawnBtn->SetVisible(!hardcore);
  }
  if (ReturnToMenuBtn)
  {
    ReturnToMenuBtn->SetVisible(hardcore);
  }
  Relayout();
}

void UDeathScreen::SetCause(const std::string &cause)
{
  if (CauseLabel)
  {
    const std::string fallback = "Fatal wounds";
    CauseLabel->SetText(cause.empty() ? fallback : cause);
  }
}

void UDeathScreen::Relayout()
{
  if (!Panel)
  {
    return;
  }
  const int w = 360;
  const int h = Hardcore ? 250 : 220;
  const int x = (ViewportW - w) / 2;
  const int y = (ViewportH - h) / 2;
  Panel->SetBounds({x, y, w, h});
  if (Title)
  {
    Title->SetBounds({x + 24, y + 24, w - 48, 32});
  }
  if (CauseLabel)
  {
    CauseLabel->SetBounds({x + 24, y + 64, w - 48, 24});
  }
  if (RespawnBtn)
  {
    RespawnBtn->SetBounds({x + 40, y + 105, w - 80, 36});
  }
  if (SpectateBtn)
  {
    SpectateBtn->SetBounds(
        {x + 40, y + (Hardcore ? 112 : 151), w - 80, 36});
  }
  if (ReturnToMenuBtn)
  {
    ReturnToMenuBtn->SetBounds({x + 40, y + 158, w - 80, 36});
  }
}

} // namespace cutum
