#ifndef CREATURE_QA_ASSET_ROOT_H
#define CREATURE_QA_ASSET_ROOT_H

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace cutum
{
// Isolated candidate assets for flight QA. Normal runs retain models/.
inline std::filesystem::path CreatureModelsRoot(
    const std::filesystem::path &workDir)
{
  const char *root = std::getenv("CUBA_CREATURE_QA_MODELS");
  if (root == nullptr || root[0] == '\0')
  {
    return workDir / "models";
  }
  const std::filesystem::path path(root);
  if (!std::filesystem::is_directory(path / "creatures") ||
      !std::filesystem::is_directory(path / "skins"))
  {
    throw std::runtime_error("CUBA_CREATURE_QA_MODELS: invalid candidate catalog");
  }
  return path;
}
} // namespace cutum
#endif
