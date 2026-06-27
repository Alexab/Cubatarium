#include "App/Platform/GameDataRoot.h"

namespace cutum
{

bool IsGameDataRoot(const std::filesystem::path &candidate)
{
  const bool hasResourcePacks =
      std::filesystem::exists(candidate / "resource_packs");
  const bool hasPrefabs = std::filesystem::exists(candidate / "prefabs");
  const bool hasShaders =
      std::filesystem::exists(candidate / "shaders" / "vshader_greedy.glsl");
  if (hasResourcePacks && hasPrefabs && hasShaders)
  {
    return true;
  }
  const bool hasTextures =
      std::filesystem::exists(candidate / "textures" / "blocks");
  const bool hasModels =
      std::filesystem::exists(candidate / "models" / "blocks");
  return hasTextures && hasModels && hasPrefabs && hasShaders;
}

std::optional<std::filesystem::path>
TryFindProjectRoot(std::filesystem::path start)
{
  for (int depth = 0; depth < kMaxProjectRootSearchDepth; ++depth)
  {
    if (IsGameDataRoot(start))
    {
      return start;
    }
    if (!start.has_parent_path())
    {
      break;
    }
    start = start.parent_path();
  }
  return std::nullopt;
}

std::filesystem::path FindProjectRoot(std::filesystem::path start)
{
  if (auto root = TryFindProjectRoot(start))
  {
    return *root;
  }
  return start;
}

} // namespace cutum
