#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace cutum
{
nlohmann::json RunAsyncIoStorageProbe(const std::filesystem::path &root);
}
