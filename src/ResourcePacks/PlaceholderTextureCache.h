#ifndef PLACEHOLDERTEXTURECACHE_H
#define PLACEHOLDERTEXTURECACHE_H

#include "Render/Textures/TextureBase.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace cutum
{

class UPlaceholderTextureCache
{
public:
  UPlaceholderTextureCache(std::filesystem::path cacheDir, int tileSize,
                           glm::vec3 background);

  std::string GetOrCreateStem(const std::string &blockName, int faceIndex,
                              int tileSize);
  void RegisterAll(UTextureBaseStorage &storage) const;

private:
  std::string MakeKey(const std::string &blockName, int faceIndex,
                      int tileSize) const;
  std::string MakeStem(const std::string &key) const;
  TexturePixelData Rasterize(const std::string &blockName, int faceIndex,
                             int tileSize) const;

  std::filesystem::path CacheDir;
  glm::vec3 Background;
  int DefaultTileSize;
  std::unordered_map<std::string, std::string> KeyToStem;
  std::unordered_map<std::string, TexturePixelData> StemPixels;
};

} // namespace cutum

#endif
