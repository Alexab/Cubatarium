#ifndef TEXTUREBASE_H
#define TEXTUREBASE_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

struct TexturePixelData
{
  int Width{0};
  int Height{0};
  std::vector<uint8_t> Rgba;
};

class UTextureBase
{
public:
  UTextureBase();
  UTextureBase(const std::string &Name, const std::string &file_name);
  UTextureBase(const std::string &Name, TexturePixelData pixels);

  const std::string &GetName() const;
  const std::string &GetFileName() const;
  bool HasPixelData() const;
  const TexturePixelData *GetPixelData() const;

private:
  std::string Name;
  std::string FileName;
  std::optional<TexturePixelData> Pixels;
};

class UTextureBaseStorage
{
public:
  void Load(const std::string &textures_path);
  void Clear();
  void Register(const std::string &stem, const std::string &file_path);
  void RegisterPixels(const std::string &stem, TexturePixelData pixels);

  const std::map<std::string, UTextureBase> &GetBaseTextures() const;

private:
  std::map<std::string, UTextureBase> BaseTextures;
};

} // namespace cutum

#endif
