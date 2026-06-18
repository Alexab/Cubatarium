#include "Gui/Widgets/ResourcePackPickerForm.h"
#include "Gui/Widgets/GuiCheckList.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Core/GuiTheme.h"
#include <sstream>

namespace cutum
{

namespace
{

constexpr int kHintH = 28;
constexpr int kPad = 8;
constexpr int kVisibleRows = 8;

} // namespace

UResourcePackPickerForm::UResourcePackPickerForm(const GuiTheme *theme)
    : Theme(theme)
{
}

int UResourcePackPickerForm::VisibleListHeight(const GuiTheme *theme)
{
  const int rowH = theme ? theme->FontSizeBody + 4 : 20;
  return kVisibleRows * rowH;
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
  SyncListItems();
}

void UResourcePackPickerForm::SetSelection(
    const std::vector<std::string> &selectedIds)
{
  SelectedIds = selectedIds;
  if (List)
  {
    List->SetCheckedIds(SelectedIds);
  }
}

std::vector<std::string> UResourcePackPickerForm::ReadSelection() const
{
  if (List)
  {
    return List->GetCheckedIds();
  }
  return SelectedIds;
}

void UResourcePackPickerForm::SyncListItems()
{
  if (!List)
  {
    return;
  }
  std::vector<GuiCheckListItem> items;
  items.reserve(InstalledPacks.size());
  for (const auto &pack : InstalledPacks)
  {
    GuiCheckListItem item;
    item.Id = pack.Id;
    item.Label = FormatPackLabel(pack);
    items.push_back(std::move(item));
  }
  List->SetItems(std::move(items));
  List->SetCheckedIds(SelectedIds);
}

void UResourcePackPickerForm::BuildInto(UGuiPanel &panel)
{
  if (!Built)
  {
    auto hint = std::make_unique<UGuiLabel>(
        Theme, "Select one or more installed resource packs.");
    HintLabel = hint.get();
    panel.AddChild(std::move(hint));

    auto list = std::make_unique<UGuiCheckList>(Theme);
    List = list.get();
    panel.AddChild(std::move(list));
    Built = true;
  }
  SyncListItems();
}

int UResourcePackPickerForm::MeasureHeight(const GuiRect & /*area*/) const
{
  return kHintH + VisibleListHeight(Theme) + kPad;
}

void UResourcePackPickerForm::Layout(const GuiRect &area) const
{
  int y = area.Y;
  if (HintLabel)
  {
    HintLabel->SetBounds({area.X, y, area.W, kHintH});
    y += kHintH;
  }
  if (List)
  {
    const int listH = VisibleListHeight(Theme);
    List->SetBounds({area.X, y, area.W, listH});
  }
}

} // namespace cutum
