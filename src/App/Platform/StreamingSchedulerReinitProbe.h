#pragma once
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
namespace cutum {
class UCore;
nlohmann::json RunStreamingSchedulerReinitProbe(const std::filesystem::path &replacement_root);
void RunStreamingGenerationShutdownProbe(UCore &core);
}
