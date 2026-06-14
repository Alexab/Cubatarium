#ifndef DESKTOP_PLATFORM_PATHS_H
#define DESKTOP_PLATFORM_PATHS_H

#include "App/Platform/IPlatformPaths.h"

namespace cutum
{

class DesktopPlatformPaths : public IPlatformPaths
{
public:
  std::filesystem::path WritableRoot() const override;
  std::filesystem::path AssetRoot() const override;
  bool ReadAssetText(const std::string &rel, std::string &out) const override;
  std::unique_ptr<std::istream> OpenAsset(const std::string &rel) const override;
  bool AssetExists(const std::string &rel) const override;
  std::filesystem::path ResolveWritable(const std::string &rel) const override;

private:
  mutable std::filesystem::path cachedRoot_;
  std::filesystem::path ProjectRoot() const;
};

} // namespace cutum

#endif
