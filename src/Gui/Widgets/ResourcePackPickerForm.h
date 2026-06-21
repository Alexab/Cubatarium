#pragma once

#include "Gui/Core/GuiTypes.h"
#include "Gui/Widgets/GuiCheckList.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;

class UResourcePackPickerForm
{
public:
  explicit UResourcePackPickerForm(const GuiTheme *theme);

  void SetPacks(const std::vector<InstalledPackInfo> &packs);
  void SetSelection(const ResourcePackSelection &selection);
  void SetSelection(const std::vector<std::string> &selectedIds);
  ResourcePackSelection ReadSelection() const;
  bool HasValidPrimarySelection() const;
  void SetOnLayoutChanged(std::function<void()> handler);

  void BuildInto(UGuiPanel &panel);
  int MeasureHeight(const GuiRect &area) const;
  void Layout(const GuiRect &area) const;

  static int VisibleListHeight(const GuiTheme *theme);

private:
  void SyncListItems();
  void UpdateWarnings();
  static std::string FormatPackLabel(const InstalledPackInfo &pack);
  static std::vector<InstalledPackInfo>
  FilterByRole(const std::vector<InstalledPackInfo> &packs, WorldgenRole role);

  const GuiTheme *Theme;
  std::vector<InstalledPackInfo> InstalledPacks;
  ResourcePackSelection Selection;
  bool Built{false};
  std::function<void()> OnLayoutChanged;
  mutable bool HasLastLayoutArea{false};
  mutable GuiRect LastLayoutArea{};

  UGuiLabel *PrimaryHintLabel{nullptr};
  UGuiLabel *SecondaryHintLabel{nullptr};
  UGuiLabel *PriorityHintLabel{nullptr};
  UGuiLabel *WarningLabel{nullptr};
  UGuiCheckList *PrimaryList{nullptr};
  UGuiCheckList *SecondaryList{nullptr};
  UGuiPanel *ContainerPanel{nullptr};
};

} // namespace cutum
