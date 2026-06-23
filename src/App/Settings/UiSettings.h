#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <string>

namespace cutum
{

enum class ControlScheme
{
  Classic,
  Cubatarium,
};

struct UiSettings
{
  bool LegacyHud{false};
  bool ShowPerformance{true};
  std::string ConsoleKey{"grave"};
  std::string PaletteKey{"b"};
  std::string InventoryKey{"e"};
  int HotbarCount{1};

  ControlScheme ControlScheme{ControlScheme::Classic};
  float PlaceClickMaxSeconds{0.20f};
  float BreakHoldMinSeconds{0.50f};
  float BreakDurationSeconds{0.25f};
  /// Cubatarium only: RMB drag distance before treating as camera look.
  int RmbDragThresholdPx{4};
};

ControlScheme ControlSchemeFromString(const std::string &value);
const char *ControlSchemeToString(ControlScheme scheme);

} // namespace cutum

#endif
