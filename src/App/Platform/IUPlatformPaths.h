#ifndef IU_PLATFORM_PATHS_H
#define IU_PLATFORM_PATHS_H

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

namespace cutum
{

class IUPlatformPaths
{
public:
  virtual ~IUPlatformPaths() = default;

  virtual std::filesystem::path WritableRoot() const = 0;
  virtual std::filesystem::path AssetRoot() const = 0;
  virtual bool ReadAssetText(const std::string &rel,
                             std::string &out) const = 0;
  virtual std::unique_ptr<std::istream>
  OpenAsset(const std::string &rel) const = 0;
  virtual bool AssetExists(const std::string &rel) const = 0;
  virtual std::filesystem::path
  ResolveWritable(const std::string &rel) const = 0;

  static void SetGlobal(std::shared_ptr<IUPlatformPaths> paths);
  static IUPlatformPaths &Current();
  static IUPlatformPaths *TryGet();

private:
  static std::shared_ptr<IUPlatformPaths> Global;
};

} // namespace cutum

#endif
