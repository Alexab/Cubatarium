#include "App/Platform/AndroidPlatformPaths.h"

#include "App/Platform/Log.h"
#include "android_jni.h"

#include <android/asset_manager.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace cutum
{

namespace
{

class UAssetStreamBuf : public std::streambuf
{
public:
  explicit UAssetStreamBuf(AAsset *asset) : asset_(asset)
  {
    const off_t length = AAsset_getLength(asset_);
    if (length > 0)
    {
      Buffer.resize(static_cast<size_t>(length));
      AAsset_read(asset_, Buffer.data(), static_cast<size_t>(length));
      setg(Buffer.data(), Buffer.data(), Buffer.data() + Buffer.size());
    }
  }

  ~UAssetStreamBuf() override
  {
    if (asset_)
    {
      AAsset_close(asset_);
    }
  }

private:
  AAsset *asset_{nullptr};
  std::vector<char> Buffer;
};

} // namespace

UAndroidPlatformPaths::UAndroidPlatformPaths(AAssetManager *assetManager)
    : AssetManager(assetManager)
{
}

std::filesystem::path UAndroidPlatformPaths::WritableRoot() const
{
  if (WritableRootPath.empty())
  {
    WritableRootPath = CubatariumAndroidGetFilesDir();
  }
  return WritableRootPath;
}

std::filesystem::path UAndroidPlatformPaths::AssetRoot() const
{
  return WritableRoot() / "game";
}

bool UAndroidPlatformPaths::AssetExists(const std::string &rel) const
{
  const auto disk = AssetRoot() / rel;
  if (std::filesystem::exists(disk))
  {
    return true;
  }
  if (!AssetManager)
  {
    return false;
  }
  AAsset *asset =
      AAssetManager_open(AssetManager, rel.c_str(), AASSET_MODE_UNKNOWN);
  if (!asset)
  {
    return false;
  }
  AAsset_close(asset);
  return true;
}

std::filesystem::path
UAndroidPlatformPaths::ResolveWritable(const std::string &rel) const
{
  return WritableRoot() / rel;
}

bool UAndroidPlatformPaths::ReadAssetText(const std::string &rel,
                                          std::string &out) const
{
  const auto disk = AssetRoot() / rel;
  if (std::filesystem::exists(disk))
  {
    std::ifstream file(disk, std::ios::binary);
    if (!file)
    {
      return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
  }
  if (!AssetManager)
  {
    return false;
  }
  AAsset *asset =
      AAssetManager_open(AssetManager, rel.c_str(), AASSET_MODE_BUFFER);
  if (!asset)
  {
    return false;
  }
  const off_t length = AAsset_getLength(asset);
  if (length <= 0)
  {
    AAsset_close(asset);
    out.clear();
    return true;
  }
  out.resize(static_cast<size_t>(length));
  const int read = AAsset_read(asset, out.data(), static_cast<size_t>(length));
  AAsset_close(asset);
  return read == length;
}

std::unique_ptr<std::istream>
UAndroidPlatformPaths::OpenAsset(const std::string &rel) const
{
  if (!AssetManager)
  {
    return nullptr;
  }
  AAsset *asset =
      AAssetManager_open(AssetManager, rel.c_str(), AASSET_MODE_BUFFER);
  if (!asset)
  {
    return nullptr;
  }
  auto stream = std::make_unique<std::istream>(new UAssetStreamBuf(asset));
  return stream;
}

void UAndroidPlatformPaths::EnsureWritableConfig() const
{
  const auto configPath = ResolveWritable("config.json");
  if (std::filesystem::exists(configPath))
  {
    return;
  }
  std::string templateText;
  if (!ReadAssetText("config.json.example", templateText))
  {
    CubatariumLogError("Asset", "Missing config.json.example in APK assets");
    return;
  }
  std::filesystem::create_directories(configPath.parent_path());
  std::ofstream out(configPath, std::ios::binary);
  out << templateText;
}

} // namespace cutum
