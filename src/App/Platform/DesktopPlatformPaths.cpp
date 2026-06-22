#include "App/Platform/DesktopPlatformPaths.h"

#include "App/Core.h"

#include <fstream>
#include <sstream>

namespace cutum
{

namespace
{

bool IsGameDataRoot(const std::filesystem::path &candidate)
{
  const bool hasResourcePacks =
      std::filesystem::exists(candidate / "resource_packs");
  const bool hasPrefabs = std::filesystem::exists(candidate / "prefabs");
  const bool hasShaders = std::filesystem::exists(candidate / "shaders" /
                                                "vshader_greedy.glsl");
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

std::filesystem::path FindGameDataRoot(const std::filesystem::path &start)
{
  auto path = start;
  for (int depth = 0; depth < 8; ++depth)
  {
    if (IsGameDataRoot(path))
    {
      return path;
    }
    if (!path.has_parent_path())
    {
      break;
    }
    path = path.parent_path();
  }
  return start;
}

} // namespace

std::filesystem::path UDesktopPlatformPaths::ProjectRoot() const
{
  if (!CachedRoot.empty())
  {
    return CachedRoot;
  }
  const auto exeDir = GetExecutableDirectory();
  const auto cwd = std::filesystem::current_path();
  for (const auto &start : {exeDir, cwd})
  {
    const auto root = FindGameDataRoot(start);
    if (IsGameDataRoot(root))
    {
      CachedRoot = root;
      return CachedRoot;
    }
  }
  CachedRoot = exeDir;
  return CachedRoot;
}

std::filesystem::path UDesktopPlatformPaths::WritableRoot() const
{
  return GetExecutableDirectory();
}

std::filesystem::path UDesktopPlatformPaths::AssetRoot() const
{
  return ProjectRoot();
}

bool UDesktopPlatformPaths::AssetExists(const std::string &rel) const
{
  return std::filesystem::exists(ProjectRoot() / rel);
}

std::filesystem::path
UDesktopPlatformPaths::ResolveWritable(const std::string &rel) const
{
  return WritableRoot() / rel;
}

bool UDesktopPlatformPaths::ReadAssetText(const std::string &rel,
                                          std::string &out) const
{
  const auto path = ProjectRoot() / rel;
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

std::unique_ptr<std::istream>
UDesktopPlatformPaths::OpenAsset(const std::string &rel) const
{
  return std::make_unique<std::ifstream>((ProjectRoot() / rel).string(),
                                         std::ios::binary);
}

} // namespace cutum
