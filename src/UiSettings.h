#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <string>

namespace cutum {

enum class BlockInputProfile {
    Classic,
    Cubatarium,
};

struct UiSettings {
    bool legacyHud{false};
    std::string consoleKey{"grave"};
    std::string paletteKey{"b"};
    std::string inventoryKey{"e"};
    int hotbarCount{1};

    BlockInputProfile blockInputProfile{BlockInputProfile::Classic};
    float placeClickMaxSeconds{0.20f};
    float breakHoldMinSeconds{0.50f};
    float breakDurationSeconds{0.25f};
    int rmbDragThresholdPx{4};
};

BlockInputProfile BlockInputProfileFromString(const std::string& value);
const char* BlockInputProfileToString(BlockInputProfile profile);

} // namespace cutum

#endif
