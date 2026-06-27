#pragma once

#include <filesystem>
#include <optional>

namespace cutum
{

constexpr int kMaxProjectRootSearchDepth = 8;

bool IsGameDataRoot(const std::filesystem::path &candidate);

std::optional<std::filesystem::path>
TryFindProjectRoot(std::filesystem::path start);

std::filesystem::path FindProjectRoot(std::filesystem::path start);

} // namespace cutum
