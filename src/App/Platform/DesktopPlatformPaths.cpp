#include "App/Platform/DesktopPlatformPaths.h"

#include "App/Core.h"

#include <fstream>
#include <sstream>

namespace cutum
{

std::filesystem::path DesktopPlatformPaths::ProjectRoot() const
{
  if (!cachedRoot_.empty())
  {
    return cachedRoot_;
  }
  const auto exeDir = GetExecutableDirectory();
  const auto cwd = std::filesystem::current_path();
  for (const auto &start : {exeDir, cwd})
  {
    auto path = start;
    for (int depth = 0; depth < 8; ++depth)
    {
      if (std::filesystem::exists(path / "textures" / "blocks") &&
          std::filesystem::exists(path / "models" / "blocks") &&
          std::filesystem::exists(path / "prefabs") &&
          std::filesystem::exists(path / "shaders" / "vshader_greedy.glsl"))
      {
        cachedRoot_ = path;
        return cachedRoot_;
      }
      if (!path.has_parent_path())
      {
        break;
      }
      path = path.parent_path();
    }
  }
  cachedRoot_ = exeDir;
  return cachedRoot_;
}

std::filesystem::path DesktopPlatformPaths::WritableRoot() const
{
  return GetExecutableDirectory();
}

std::filesystem::path DesktopPlatformPaths::AssetRoot() const { return ProjectRoot(); }

bool DesktopPlatformPaths::AssetExists(const std::string &rel) const
{
  return std::filesystem::exists(ProjectRoot() / rel);
}

std::filesystem::path
DesktopPlatformPaths::ResolveWritable(const std::string &rel) const
{
  return WritableRoot() / rel;
}

bool DesktopPlatformPaths::ReadAssetText(const std::string &rel,
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
DesktopPlatformPaths::OpenAsset(const std::string &rel) const
{
  return std::make_unique<std::ifstream>((ProjectRoot() / rel).string(),
                                          std::ios::binary);
}

} // namespace cutum
