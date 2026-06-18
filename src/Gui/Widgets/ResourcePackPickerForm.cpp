#include "Gui/Widgets/ResourcePackPickerForm.h"
#include "Gui/Widgets/GuiCheckList.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Core/GuiTheme.h"
#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{

constexpr int kHintH = 22;
constexpr int kWarnH = 36;
constexpr int kPad = 6;
constexpr int kVisibleRows = 5;

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

std::vector<InstalledPackInfo> UResourcePackPickerForm::FilterByRole(
    const std::vector<InstalledPackInfo> &packs, WorldgenRole role)
{
  std::vector<InstalledPackInfo> out;
  for (const auto &pack : packs)
  {
    if (pack.Role == role)
    {
      out.push_back(pack);
    }
  }
  return out;
}

void UResourcePackPickerForm::SetPacks(
    const std::vector<InstalledPackInfo> &packs)
{
  InstalledPacks = packs;
  SyncListItems();
}

void UResourcePackPickerForm::SetSelection(
    const ResourcePackSelection &selection)
{
  Selection = selection;
  if (PrimaryList)
  {
    PrimaryList->SetCheckedIds(Selection.Primary);
  }
  if (SecondaryList)
  {
    SecondaryList->SetCheckedIds(Selection.Secondary);
  }
}

void UResourcePackPickerForm::SetSelection(
    const std::vector<std::string> &selectedIds)
{
  ResourcePackSelection selection;
  selection.Primary = selectedIds;
  SetSelection(selection);
}

ResourcePackSelection UResourcePackPickerForm::ReadSelection() const
{
  ResourcePackSelection result = Selection;
  if (PrimaryList)
  {
    result.Primary = PrimaryList->GetCheckedIds();
  }
  if (SecondaryList)
  {
    result.Secondary = SecondaryList->GetCheckedIds();
  }
  return result;
}

bool UResourcePackPickerForm::HasValidPrimarySelection() const
{
  return !ReadSelection().Primary.empty();
}

void UResourcePackPickerForm::SyncListItems()
{
  const auto primaryPacks =
      FilterByRole(InstalledPacks, WorldgenRole::Primary);
  const auto secondaryPacks =
      FilterByRole(InstalledPacks, WorldgenRole::Secondary);

  auto fillList = [](UGuiCheckList *list,
                     const std::vector<InstalledPackInfo> &packs,
                     const std::vector<std::string> &checked) {
    if (!list)
    {
      return;
    }
    std::vector<GuiCheckListItem> items;
    items.reserve(packs.size());
    for (const auto &pack : packs)
    {
      GuiCheckListItem item;
      item.Id = pack.Id;
      item.Label = FormatPackLabel(pack);
      items.push_back(std::move(item));
    }
    list->SetItems(std::move(items));
    list->SetCheckedIds(checked);
  };

  fillList(PrimaryList, primaryPacks, Selection.Primary);
  fillList(SecondaryList, secondaryPacks, Selection.Secondary);

  auto wireList = [this](UGuiCheckList *list) {
    if (!list)
    {
      return;
    }
    list->SetOnChanged([this]() { UpdateWarnings(); });
  };
  wireList(PrimaryList);
  wireList(SecondaryList);
  UpdateWarnings();

  if (PrimaryHintLabel)
  {
    std::ostringstream hint;
    hint << "Primary packs (required, " << primaryPacks.size()
         << " available) — worldgen blocks";
    PrimaryHintLabel->SetText(hint.str());
  }
  if (SecondaryHintLabel)
  {
    std::ostringstream hint;
    hint << "Secondary packs (optional, " << secondaryPacks.size()
         << " available) — extra blocks only";
    SecondaryHintLabel->SetText(hint.str());
  }
  if (PriorityHintLabel)
  {
    PriorityHintLabel->SetText(
        "Higher in the list = higher priority (like Minecraft). "
        "Drag rows or Ctrl+Up/Down to reorder.");
  }
}

void UResourcePackPickerForm::UpdateWarnings()
{
  if (!WarningLabel)
  {
    return;
  }
  const ResourcePackSelection current = ReadSelection();
  std::vector<std::string> enabled = current.Primary;
  enabled.insert(enabled.end(), current.Secondary.begin(),
                 current.Secondary.end());

  auto findPack = [&](const std::string &id) -> const InstalledPackInfo * {
    for (const auto &pack : InstalledPacks)
    {
      if (pack.Id == id)
      {
        return &pack;
      }
    }
    return nullptr;
  };

  std::vector<std::string> warnings;
  for (const std::string &id : enabled)
  {
    const InstalledPackInfo *pack = findPack(id);
    if (!pack)
    {
      continue;
    }
    for (const std::string &dep : pack->Depends)
    {
      if (std::find(enabled.begin(), enabled.end(), dep) == enabled.end())
      {
        warnings.push_back(pack->Id + " requires " + dep);
      }
    }
    for (const std::string &conflict : pack->Conflicts)
    {
      if (std::find(enabled.begin(), enabled.end(), conflict) != enabled.end())
      {
        warnings.push_back(pack->Id + " conflicts with " + conflict);
      }
    }
  }

  if (warnings.empty())
  {
    WarningLabel->SetText("");
    WarningLabel->SetVisible(false);
    return;
  }
  std::ostringstream oss;
  oss << "WARN: ";
  for (size_t i = 0; i < warnings.size(); ++i)
  {
    if (i > 0)
    {
      oss << "; ";
    }
    oss << warnings[i];
  }
  WarningLabel->SetText(oss.str());
  WarningLabel->SetVisible(true);
}

void UResourcePackPickerForm::BuildInto(UGuiPanel &panel)
{
  if (!Built)
  {
    auto priorityHint =
        std::make_unique<UGuiLabel>(Theme, "Higher in the list = higher priority (like Minecraft).");
    PriorityHintLabel = priorityHint.get();
    panel.AddChild(std::move(priorityHint));

    auto primaryHint = std::make_unique<UGuiLabel>(Theme, "Primary packs");
    PrimaryHintLabel = primaryHint.get();
    panel.AddChild(std::move(primaryHint));

    auto primaryList = std::make_unique<UGuiCheckList>(Theme);
    PrimaryList = primaryList.get();
    panel.AddChild(std::move(primaryList));

    auto secondaryHint = std::make_unique<UGuiLabel>(Theme, "Secondary packs");
    SecondaryHintLabel = secondaryHint.get();
    panel.AddChild(std::move(secondaryHint));

    auto secondaryList = std::make_unique<UGuiCheckList>(Theme);
    SecondaryList = secondaryList.get();
    panel.AddChild(std::move(secondaryList));

    auto warn = std::make_unique<UGuiLabel>(Theme, "");
    warn->SetVisible(false);
    WarningLabel = warn.get();
    panel.AddChild(std::move(warn));

    Built = true;
  }
  SyncListItems();
}

int UResourcePackPickerForm::MeasureHeight(const GuiRect & /*area*/) const
{
  const int warnExtra =
      (WarningLabel && WarningLabel->IsVisible()) ? kWarnH : 0;
  return kHintH + VisibleListHeight(Theme) + kHintH + VisibleListHeight(Theme) +
         kHintH + warnExtra + kPad * 2;
}

void UResourcePackPickerForm::Layout(const GuiRect &area) const
{
  int y = area.Y;
  if (PriorityHintLabel)
  {
    PriorityHintLabel->SetBounds({area.X, y, area.W, kHintH});
    y += kHintH;
  }
  if (PrimaryHintLabel)
  {
    PrimaryHintLabel->SetBounds({area.X, y, area.W, kHintH});
    y += kHintH;
  }
  if (PrimaryList)
  {
    const int listH = VisibleListHeight(Theme);
    PrimaryList->SetBounds({area.X, y, area.W, listH});
    y += listH + kPad;
  }
  if (SecondaryHintLabel)
  {
    SecondaryHintLabel->SetBounds({area.X, y, area.W, kHintH});
    y += kHintH;
  }
  if (SecondaryList)
  {
    const int listH = VisibleListHeight(Theme);
    SecondaryList->SetBounds({area.X, y, area.W, listH});
    y += listH + kPad;
  }
  if (WarningLabel && WarningLabel->IsVisible())
  {
    WarningLabel->SetBounds({area.X, y, area.W, kWarnH});
  }
}

} // namespace cutum
