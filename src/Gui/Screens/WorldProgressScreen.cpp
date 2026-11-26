#include "Gui/Screens/WorldProgressScreen.h"
#include "App/StoredApplicationActions.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiProgressBar.h"
#include <algorithm>
#include <sstream>
#include <type_traits>
#include <typeinfo>

namespace cutum
{

namespace
{

constexpr int kCardWDesign = 420;
constexpr int kCardHMinDesign = 160;
constexpr int kStackSpacingDesign = 10;
constexpr int kStackPaddingDesign = 16;

const char *TitleForKind(WorldOperationKind kind)
{
  switch (kind)
  {
  case WorldOperationKind::Load:
    return "Loading world...";
  case WorldOperationKind::Create:
    return "Creating world...";
  case WorldOperationKind::Save:
    return "Saving world...";
  case WorldOperationKind::EnterGame:
    return "Starting game...";
  case WorldOperationKind::Shutdown:
    return "Shutting down...";
  }
  return "Please wait...";
}

} // namespace

void UWorldProgressScreen::Build(UGuiContext &ctx)
{
  auto backdrop = std::make_unique<UGuiPanel>(&ctx.GetTheme());
  backdrop->SetDrawBackground(true);
  backdrop->SetBounds(GuiRect{0, 0, ViewportW, ViewportH});

  auto card = std::make_unique<UGuiPanel>(&ctx.GetTheme());
  card->SetStackLayout(kStackSpacingDesign, kStackPaddingDesign);
  card->SetDrawBackground(true);

  auto title = std::make_unique<UGuiLabel>(&ctx.GetTheme(), TitleForKind(
                                                              WorldOperationKind::Load));
  title->SetTextAlign(GuiTextAlign::Center);
  auto *title_label = title.get();
  card->AddChild(std::move(title));

  auto phase = std::make_unique<UGuiLabel>(&ctx.GetTheme(), "");
  phase->SetTextAlign(GuiTextAlign::Center);
  phase->SetUseSecondaryColor(true);
  auto *phase_label = phase.get();
  card->AddChild(std::move(phase));

  auto substage = std::make_unique<UGuiLabel>(&ctx.GetTheme(), " ");
  substage->SetTextAlign(GuiTextAlign::Center);
  substage->SetUseSecondaryColor(true);
  auto *substage_label = substage.get();
  card->AddChild(std::move(substage));

  auto bar = std::make_unique<UGuiProgressBar>(&ctx.GetTheme());
  bar->SetIndeterminate(true);
  bar->SetShowPercent(true);
  auto *progress_bar = bar.get();
  card->AddChild(std::move(bar));

  auto retry = std::make_unique<UGuiButton>(&ctx.GetTheme(), "Retry save");
  retry->SetVisible(false);
  auto *retry_button = retry.get();
  card->AddChild(std::move(retry));
  auto back = std::make_unique<UGuiButton>(&ctx.GetTheme(), "Back to menu");
  back->SetVisible(false);
  auto *back_button = back.get();
  card->AddChild(std::move(back));

  backdrop->AddChild(std::move(card));
  Root = std::move(backdrop);
  TitleLabel = title_label;
  PhaseLabel = phase_label;
  SubstageLabel = substage_label;
  ProgressBar = progress_bar;
  RetryButton = retry_button;
  BackButton = back_button;
  LayoutCentered();
}

WorldProgressScreenStorage UWorldProgressScreen::InspectOwnedStorage(
    const UGuiContext &context, const UApplication *application) const noexcept
{
  WorldProgressScreenStorage out;
  if (typeid(*this) != typeid(UWorldProgressScreen) || context.GetScreen() != this ||
      Context != &context || !Root || typeid(*Root) != typeid(UGuiPanel))
    return out;

  const auto &outer = static_cast<const UGuiPanel &>(*Root);
  const auto &outer_children = outer.GetChildren();
  if (outer_children.size() != 1 || !outer_children[0] ||
      typeid(*outer_children[0]) != typeid(UGuiPanel))
    return out;
  const auto &card = static_cast<const UGuiPanel &>(*outer_children[0]);
  const auto &children = card.GetChildren();
  if (children.size() != 6)
    return out;

  const auto *theme = &context.GetTheme();
  const auto matches = [theme](const auto &child, const auto *alias) {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(alias)>>;
    return child && child.get() == alias && typeid(*child) == typeid(Widget) &&
           child->GetChildren().empty() && alias->Theme == theme;
  };
  out.bindings_match = outer.Theme == theme && card.Theme == theme &&
      matches(children[0], TitleLabel) && matches(children[1], PhaseLabel) &&
      matches(children[2], SubstageLabel) && matches(children[3], ProgressBar) &&
      matches(children[4], RetryButton) && matches(children[5], BackButton);
  if (!out.bindings_match)
    return out;

  out.screen_bytes = sizeof(*this);
  out.widgets = 2 + children.size();
  out.widget_bytes = 2 * sizeof(UGuiPanel) + 3 * sizeof(UGuiLabel) +
                     sizeof(UGuiProgressBar) + 2 * sizeof(UGuiButton);
  StlSourceStorage slots, texts, total;
  const auto count_children = [&slots](const UGuiWidget &widget) {
    slots.Buffer(widget.GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  };
  count_children(outer);
  count_children(card);
  for (const auto &child : children)
    count_children(*child);
  texts.Text(FailureMessage);
  for (const auto *label : {TitleLabel, PhaseLabel, SubstageLabel})
    texts.Text(label->GetText());
  texts.Text(ProgressBar->GetLabel());
  texts.Text(RetryButton->GetLabel());
  texts.Text(BackButton->GetLabel());
  const auto retry = InspectKnownOwnerCallback<ApplicationScheduleRetrySaveAction>(
      RetryButton->OnClick, application, StlSourceStorage::SupportedProfile());
  const auto back = InspectKnownOwnerCallback<ApplicationScheduleShowMenuAction>(
      BackButton->OnClick, application, StlSourceStorage::SupportedProfile());
  out.callback_present = {retry.present, back.present};
  out.callback_supported = {retry.supported, back.supported};
  out.callback_bytes = retry.requests + back.requests;
  out.children_bytes = slots.bytes.requested;
  out.text_bytes = texts.bytes.requested;
  for (const auto amount : {out.screen_bytes, out.widget_bytes, out.children_bytes,
                            out.text_bytes, out.callback_bytes})
    total.Add(amount);
  out.requested = total.bytes.requested;
  out.supported = slots.bytes.supported && texts.bytes.supported && total.bytes.supported &&
                  retry.supported && back.supported;
  return out;
}

void UWorldProgressScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  LayoutCentered();
}

void UWorldProgressScreen::LayoutCentered()
{
  if (!Root || Root->GetChildren().empty())
  {
    return;
  }
  Root->SetBounds(GuiRect{0, 0, ViewportW, ViewportH});
  UGuiWidget *card = Root->GetChildren().front().get();
  const int cardW = std::min(Scaled(kCardWDesign), std::max(1, ViewportW));
  if (!FailureMessage.empty() && Context && PhaseLabel)
  {
    const int width = std::max(1, cardW - 2 * Scaled(kStackPaddingDesign) -
                                      2 * Context->GetTheme().Padding);
    std::istringstream words(FailureMessage);
    std::string word, line, wrapped;
    while (words >> word)
    {
      const auto candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() &&
          Context->GetRenderer().MeasureTextWidth(candidate) > width)
      {
        wrapped += line + '\n';
        line = word;
      }
      else
        line = candidate;
    }
    PhaseLabel->SetText(wrapped + line);
  }

  std::vector<UGuiWidget *> stackChildren;
  stackChildren.reserve(card->GetChildren().size());
  for (const auto &child : card->GetChildren())
  {
    if (child->IsVisible())
    {
      stackChildren.push_back(child.get());
    }
  }

  const int spacing = Scaled(kStackSpacingDesign);
  const int padding = Scaled(kStackPaddingDesign);
  const int contentH = UGuiLayout::StackVerticalMeasure(
      {0, 0, cardW, 100000}, spacing, padding, stackChildren);
  const int cardH =
      std::min(std::max(Scaled(kCardHMinDesign), contentH), std::max(1, ViewportH));
  const int x = std::max(0, (ViewportW - cardW) / 2);
  const int y = std::max(0, (ViewportH - cardH) / 2);
  card->SetBounds(GuiRect{x, y, cardW, cardH});
  UGuiLayout::StackVertical(card->GetBounds(), spacing, padding, stackChildren);
}

void UWorldProgressScreen::ApplySnapshot(const ProgressSnapshot &snapshot)
{
  const bool failed = !snapshot.active && !snapshot.succeeded;
  FailureMessage = failed ? snapshot.errorMessage : std::string{};
  if (TitleLabel)
  {
    TitleLabel->SetText(failed ? "Save failed" : TitleForKind(snapshot.kind));
  }
  if (PhaseLabel)
  {
    PhaseLabel->SetText(snapshot.message.empty() ? " " : snapshot.message);
  }
  if (SubstageLabel)
  {
    // Structured counters from the snapshot - no message-string parsing.
    SubstageLabel->SetText(failed ? "Your changes are still in memory."
                           : snapshot.subTotal > 0
                               ? std::to_string(std::max(0, snapshot.subDone)) +
                                     "/" + std::to_string(snapshot.subTotal)
                               : std::string(" "));
  }
  if (ProgressBar)
  {
    ProgressBar->SetVisible(!failed);
    if (snapshot.fraction < 0.f)
    {
      ProgressBar->SetIndeterminate(true);
    }
    else
    {
      ProgressBar->SetValue(snapshot.fraction);
    }
  }
  LayoutCentered();
}

void UWorldProgressScreen::SetFailureActions(std::function<void()> retry,
                                             std::function<void()> back)
{
  if (RetryButton)
  {
    RetryButton->SetOnClick(std::move(retry));
    RetryButton->SetVisible(true);
  }
  if (BackButton)
  {
    BackButton->SetOnClick(std::move(back));
    BackButton->SetVisible(true);
  }
  LayoutCentered();
}

} // namespace cutum
