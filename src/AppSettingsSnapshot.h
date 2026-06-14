#pragma once

#include "RenderSettings.h"
#include "UiSettings.h"
#include <string>

namespace cutum {

struct AppSettingsSnapshot {
    std::string DefaultUser;
    std::string DefaultWorld;
    int RenderDistanceChunks{4};
    bool StreamingEnabled{true};
    bool StepUpEnabled{true};
    bool EntityCollisionEnabled{true};
    RenderSettings Render;
    UiSettings Ui;
};

} // namespace cutum
