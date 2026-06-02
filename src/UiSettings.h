#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <string>

namespace cutum {

struct UiSettings {
    bool legacyHud{false};
    std::string consoleKey{"grave"};
    std::string paletteKey{"b"};
    std::string inventoryKey{"e"};
    int hotbarCount{1};
};

} // namespace cutum

#endif
