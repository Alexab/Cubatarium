#include "Gui/Widgets/WorldViewSettingsForm.h"

#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"
#include <cstdlib>
#include <string>

namespace cutum
{

namespace
{

float ParseFloatOr(const std::string &text, float fallback)
{
  if (text.empty())
  {
    return fallback;
  }
  char *end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (end == text.c_str())
  {
    return fallback;
  }
  return value;
}

} // namespace

UWorldViewSettingsForm::UWorldViewSettingsForm(const GuiTheme *theme)
    : Theme(theme)
{
}

void UWorldViewSettingsForm::SetOnLayoutChanged(std::function<void()> handler)
{
  OnLayoutChanged = std::move(handler);
}

void UWorldViewSettingsForm::SetSettings(const WorldViewSettings &settings)
{
  FormSettings = settings;
  FormSettings.Validate();
  if (!Built)
  {
    return;
  }
  if (ProjectionList)
  {
    ProjectionList->SetSelectedIndex(
        FormSettings.Projection == WorldProjectionMode::OrthographicIsometric
            ? 1
            : 0);
  }
  if (OrthoSizeInput)
  {
    OrthoSizeInput->SetText(
        std::to_string(static_cast<int>(FormSettings.OrthoSize)));
  }
  if (ShowFpWieldCheckbox)
  {
    ShowFpWieldCheckbox->SetChecked(FormSettings.ShowFpWield);
  }
  UpdateFieldVisibility();
}

WorldViewSettings UWorldViewSettingsForm::ReadSettings() const
{
  WorldViewSettings settings = FormSettings;
  if (ProjectionList)
  {
    settings.Projection = ProjectionList->GetSelectedIndex() == 1
                              ? WorldProjectionMode::OrthographicIsometric
                              : WorldProjectionMode::Perspective;
  }
  if (OrthoSizeInput)
  {
    settings.OrthoSize =
        ParseFloatOr(OrthoSizeInput->GetText(), settings.OrthoSize);
  }
  if (ShowFpWieldCheckbox)
  {
    settings.ShowFpWield = ShowFpWieldCheckbox->IsChecked();
  }
  settings.Validate();
  return settings;
}

void UWorldViewSettingsForm::UpdateFieldVisibility()
{
  const bool iso = ProjectionList && ProjectionList->GetSelectedIndex() == 1;
  if (OrthoSizeLabel)
  {
    OrthoSizeLabel->SetVisible(iso);
  }
  if (OrthoSizeInput)
  {
    OrthoSizeInput->SetVisible(iso);
  }
  if (OnLayoutChanged)
  {
    OnLayoutChanged();
  }
}

void UWorldViewSettingsForm::BuildInto(UGuiPanel &panel)
{
  if (Built || !Theme)
  {
    return;
  }

  auto section = std::make_unique<UGuiLabel>(Theme, "View / Projection");
  SectionLabel = section.get();
  panel.AddChild(std::move(section));

  auto projCaption = std::make_unique<UGuiLabel>(
      Theme, "Perspective (FPS) or Isometric");
  ProjectionLabel = projCaption.get();
  panel.AddChild(std::move(projCaption));

  auto list = std::make_unique<UGuiListView>(Theme);
  ProjectionList = list.get();
  ProjectionList->SetItems({"Perspective (FPS)", "Isometric"});
  ProjectionList->SetVisibleRowCount(2);
  ProjectionList->SetSelectedIndex(
      FormSettings.Projection == WorldProjectionMode::OrthographicIsometric ? 1
                                                                            : 0);
  ProjectionList->SetOnSelectionChanged([this](int) { UpdateFieldVisibility(); });
  panel.AddChild(std::move(list));

  auto orthoLabel = std::make_unique<UGuiLabel>(Theme, "Ortho size (blocks)");
  OrthoSizeLabel = orthoLabel.get();
  panel.AddChild(std::move(orthoLabel));

  auto orthoInput = std::make_unique<UGuiTextInput>(Theme);
  OrthoSizeInput = orthoInput.get();
  OrthoSizeInput->SetText(
      std::to_string(static_cast<int>(FormSettings.OrthoSize)));
  panel.AddChild(std::move(orthoInput));

  auto fpWield = std::make_unique<UGuiCheckbox>(
      Theme, "Show first-person hands / tool");
  ShowFpWieldCheckbox = fpWield.get();
  ShowFpWieldCheckbox->SetChecked(FormSettings.ShowFpWield);
  ShowFpWieldCheckbox->SetDescription(
      "Screen-space viewmodel overlay in Perspective (FPS) camera.");
  panel.AddChild(std::move(fpWield));

  Built = true;
  UpdateFieldVisibility();
}

int UWorldViewSettingsForm::MeasureHeight(const GuiRect &area) const
{
  (void)area;
  if (!Theme || !Built)
  {
    return 0;
  }
  const int row = Theme->FontSizeBody + 8;
  const int gap = Theme->Padding / 2;
  int height = row + gap;
  height += row + gap;
  height += ProjectionList ? ProjectionList->GetPreferredHeight() : row * 2;
  height += gap;
  if (ProjectionList && ProjectionList->GetSelectedIndex() == 1)
  {
    height += row + gap;
    height += row + gap;
  }
  height += (ShowFpWieldCheckbox ? ShowFpWieldCheckbox->GetPreferredHeight()
                                 : row) +
            gap;
  return height;
}

void UWorldViewSettingsForm::Layout(const GuiRect &area) const
{
  if (!Theme || !Built)
  {
    return;
  }
  const int row = Theme->FontSizeBody + 8;
  const int gap = Theme->Padding / 2;
  int y = area.Y;
  const int x = area.X;
  const int w = area.W;

  auto place = [&](UGuiWidget *widget, int height)
  {
    if (!widget || !widget->IsVisible())
    {
      return;
    }
    widget->SetBounds({x, y, w, height});
    y += height + gap;
  };

  place(SectionLabel, row);
  place(ProjectionLabel, row);
  place(ProjectionList,
        ProjectionList ? ProjectionList->GetPreferredHeight() : row * 2);
  place(OrthoSizeLabel, row);
  place(OrthoSizeInput, row);
  place(ShowFpWieldCheckbox,
        ShowFpWieldCheckbox ? ShowFpWieldCheckbox->GetPreferredHeight() : row);
}

} // namespace cutum
