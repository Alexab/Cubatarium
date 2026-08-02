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
  bool ShowPerformance{false};
  /// Seconds between periodic [Perf] / perf_*.jsonl aggregates (InGame).
  float PerfLogIntervalSec{2.0f};
  std::string ConsoleKey{"grave"};
  std::string PaletteKey{"b"};      // creative palette, Blocks tab
  std::string WorldGenKey{"g"};     // world generation sets
  std::string InventoryKey{"e"};    // creative palette toggle (last main tab; default Blocks)
  std::string CharacterKey{"c"};    // character sheet overlay
  int HotbarCount{1};

  ControlScheme ControlScheme{ControlScheme::Classic};
  float PlaceClickMaxSeconds{0.20f};
  float BreakHoldMinSeconds{0.50f};
  float BreakDurationSeconds{0.25f};
  /// Cubatarium only: RMB drag distance before treating as camera look.
  int RmbDragThresholdPx{4};
  /// User multiplier applied on top of automatic UI scale (0.5–2.0).
  float UiScaleUser{1.f};
};

ControlScheme ControlSchemeFromString(const std::string &value);
const char *ControlSchemeToString(ControlScheme scheme);

} // namespace cutum

#endif
