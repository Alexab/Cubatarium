#ifndef ANDROID_PLATFORM_PATHS_H
#define ANDROID_PLATFORM_PATHS_H

#include "App/Platform/IUPlatformPaths.h"

struct AAssetManager;

namespace cutum
{

class UAndroidPlatformPaths : public IUPlatformPaths
{
public:
  explicit UAndroidPlatformPaths(AAssetManager *assetManager);

  std::filesystem::path WritableRoot() const override;
  std::filesystem::path AssetRoot() const override;
  bool ReadAssetText(const std::string &rel, std::string &out) const override;
  std::unique_ptr<std::istream>
  OpenAsset(const std::string &rel) const override;
  bool AssetExists(const std::string &rel) const override;
  std::filesystem::path ResolveWritable(const std::string &rel) const override;

  void EnsureWritableConfig() const;

private:
  AAssetManager *AssetManager;
  mutable std::filesystem::path WritableRootPath;
};

} // namespace cutum

#endif
