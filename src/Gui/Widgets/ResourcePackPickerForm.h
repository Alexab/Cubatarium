#pragma once

#include "Gui/Core/GuiTypes.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiCheckbox;

class UResourcePackPickerForm
{
public:
  explicit UResourcePackPickerForm(const GuiTheme *theme);

  void SetPacks(const std::vector<InstalledPackInfo> &packs);
  void SetSelection(const std::vector<std::string> &selectedIds);
  std::vector<std::string> ReadSelection() const;

  void BuildInto(UGuiPanel &panel);
  int MeasureHeight(const GuiRect &area) const;
  void Layout(const GuiRect &area) const;

private:
  struct PackRow
  {
    std::string Id;
    UGuiCheckbox *Checkbox{nullptr};
  };

  void AddWidgetsTo(UGuiPanel &panel);
  static std::string FormatPackLabel(const InstalledPackInfo &pack);

  const GuiTheme *Theme;
  std::vector<InstalledPackInfo> InstalledPacks;
  std::vector<std::string> SelectedIds;
  bool Built{false};

  UGuiLabel *HintLabel{nullptr};
  std::vector<PackRow> Rows;
};

} // namespace cutum
