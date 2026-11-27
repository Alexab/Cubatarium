#pragma once
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
namespace cutum {
nlohmann::json RunStreamingSchedulerReinitProbe(const std::filesystem::path &replacement_root);
}
