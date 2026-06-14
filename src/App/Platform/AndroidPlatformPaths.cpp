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

class AssetStreamBuf : public std::streambuf
{
public:
  explicit AssetStreamBuf(AAsset *asset) : asset_(asset)
  {
    const off_t length = AAsset_getLength(asset_);
    if (length > 0)
    {
      buffer_.resize(static_cast<size_t>(length));
      AAsset_read(asset_, buffer_.data(), static_cast<size_t>(length));
      setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
    }
  }

  ~AssetStreamBuf() override
  {
    if (asset_)
    {
      AAsset_close(asset_);
    }
  }

private:
  AAsset *asset_{nullptr};
  std::vector<char> buffer_;
};

} // namespace

AndroidPlatformPaths::AndroidPlatformPaths(AAssetManager *assetManager)
    : AssetManager_(assetManager)
{
}

std::filesystem::path AndroidPlatformPaths::WritableRoot() const
{
  if (writableRoot_.empty())
  {
    writableRoot_ = CubatariumAndroidGetFilesDir();
  }
  return writableRoot_;
}

std::filesystem::path AndroidPlatformPaths::AssetRoot() const
{
  return WritableRoot() / "game";
}

bool AndroidPlatformPaths::AssetExists(const std::string &rel) const
{
  const auto disk = AssetRoot() / rel;
  if (std::filesystem::exists(disk))
  {
    return true;
  }
  if (!AssetManager_)
  {
    return false;
  }
  AAsset *asset = AAssetManager_open(AssetManager_, rel.c_str(), AASSET_MODE_UNKNOWN);
  if (!asset)
  {
    return false;
  }
  AAsset_close(asset);
  return true;
}

std::filesystem::path
AndroidPlatformPaths::ResolveWritable(const std::string &rel) const
{
  return WritableRoot() / rel;
}

bool AndroidPlatformPaths::ReadAssetText(const std::string &rel,
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
  if (!AssetManager_)
  {
    return false;
  }
  AAsset *asset = AAssetManager_open(AssetManager_, rel.c_str(), AASSET_MODE_BUFFER);
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
AndroidPlatformPaths::OpenAsset(const std::string &rel) const
{
  if (!AssetManager_)
  {
    return nullptr;
  }
  AAsset *asset = AAssetManager_open(AssetManager_, rel.c_str(), AASSET_MODE_BUFFER);
  if (!asset)
  {
    return nullptr;
  }
  auto stream = std::make_unique<std::istream>(new AssetStreamBuf(asset));
  return stream;
}

void AndroidPlatformPaths::EnsureWritableConfig() const
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
