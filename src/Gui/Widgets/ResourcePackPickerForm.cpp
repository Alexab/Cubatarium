#include "Gui/Widgets/ResourcePackPickerForm.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{

bool ContainsId(const std::vector<std::string> &ids, const std::string &id)
{
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

UResourcePackPickerForm::UResourcePackPickerForm(const GuiTheme *theme)
    : Theme(theme)
{
}

std::string
UResourcePackPickerForm::FormatPackLabel(const InstalledPackInfo &pack)
{
  std::ostringstream oss;
  oss << pack.DisplayName << " (" << pack.Resolution << "px, priority "
      << pack.Priority;
  if (!pack.License.empty())
  {
    oss << ", " << pack.License;
  }
  oss << ')';
  return oss.str();
}

void UResourcePackPickerForm::SetPacks(
    const std::vector<InstalledPackInfo> &packs)
{
  InstalledPacks = packs;
}

void UResourcePackPickerForm::SetSelection(
    const std::vector<std::string> &selectedIds)
{
  SelectedIds = selectedIds;
  for (auto &row : Rows)
  {
    if (row.Checkbox)
    {
      row.Checkbox->SetChecked(ContainsId(SelectedIds, row.Id));
    }
  }
}

std::vector<std::string> UResourcePackPickerForm::ReadSelection() const
{
  std::vector<std::string> result;
  for (const auto &row : Rows)
  {
    if (row.Checkbox && row.Checkbox->IsChecked())
    {
      result.push_back(row.Id);
    }
  }
  return result;
}

void UResourcePackPickerForm::BuildInto(UGuiPanel &panel)
{
  if (!Built)
  {
    AddWidgetsTo(panel);
    Built = true;
  }
  SetSelection(SelectedIds);
}

int UResourcePackPickerForm::MeasureHeight(const GuiRect &area) const
{
  constexpr int kHintH = 28;
  constexpr int kRowH = 30;
  constexpr int kPad = 8;
  const int rows = static_cast<int>(InstalledPacks.size());
  return kHintH + rows * kRowH + kPad;
}

void UResourcePackPickerForm::Layout(const GuiRect &area) const
{
  constexpr int kHintH = 28;
  constexpr int kRowH = 30;
  int y = area.Y;
  if (HintLabel)
  {
    HintLabel->SetBounds({area.X, y, area.W, kHintH});
    y += kHintH;
  }
  for (const auto &row : Rows)
  {
    if (row.Checkbox)
    {
      row.Checkbox->SetBounds({area.X, y, area.W, kRowH});
      y += kRowH;
    }
  }
}

void UResourcePackPickerForm::AddWidgetsTo(UGuiPanel &panel)
{
  auto hint = std::make_unique<UGuiLabel>(
      Theme, "Select one or more installed resource packs.");
  HintLabel = hint.get();
  panel.AddChild(std::move(hint));

  for (const auto &pack : InstalledPacks)
  {
    PackRow row;
    row.Id = pack.Id;
    auto box = std::make_unique<UGuiCheckbox>(Theme, FormatPackLabel(pack));
    row.Checkbox = box.get();
    panel.AddChild(std::move(box));
    Rows.push_back(row);
  }
}

} // namespace cutum
