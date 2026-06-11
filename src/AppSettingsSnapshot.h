#pragma once

#include "RenderSettings.h"
#include "UiSettings.h"
#include <string>

namespace cutum {

struct AppSettingsSnapshot {
    std::string defaultUser;
    std::string defaultWorld;
    int renderDistanceChunks{4};
    bool streamingEnabled{true};
    bool stepUpEnabled{true};
    bool entityCollisionEnabled{true};
    RenderSettings render;
    UiSettings ui;
};

} // namespace cutum
