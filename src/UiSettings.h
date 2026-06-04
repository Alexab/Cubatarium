#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <string>

namespace cutum {

enum class ControlScheme {
    Classic,
    Cubatarium,
};

struct UiSettings {
    bool legacyHud{false};
    std::string consoleKey{"grave"};
    std::string paletteKey{"b"};
    std::string inventoryKey{"e"};
    int hotbarCount{1};

    ControlScheme controlScheme{ControlScheme::Classic};
    float placeClickMaxSeconds{0.20f};
    float breakHoldMinSeconds{0.50f};
    float breakDurationSeconds{0.25f};
    /// Cubatarium only: RMB drag distance before treating as camera look.
    int rmbDragThresholdPx{4};
};

ControlScheme ControlSchemeFromString(const std::string& value);
const char* ControlSchemeToString(ControlScheme scheme);

} // namespace cutum

#endif
