#ifndef PLACEHOLDERTEXTURECACHE_H
#define PLACEHOLDERTEXTURECACHE_H

#include "Render/Textures/TextureBase.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <list>
#include <string>
#include <unordered_map>

namespace cutum
{

class UPlaceholderTextureCache
{
public:
  UPlaceholderTextureCache(std::filesystem::path cacheDir, int tileSize,
                           glm::vec3 background, size_t maxEntries = 256);

  std::string GetOrCreateStem(const std::string &blockName, int faceIndex,
                              int tileSize);
  void RegisterAll(UTextureBaseStorage &storage) const;
  size_t GetEntryCount() const { return KeyToStem.size(); }
  size_t GetMaxEntries() const { return MaxEntries; }

private:
  void TouchLru(const std::string &key);
  void EvictIfNeeded();

  std::string MakeKey(const std::string &blockName, int faceIndex,
                      int tileSize) const;
  std::string MakeStem(const std::string &key) const;
  TexturePixelData Rasterize(const std::string &blockName, int faceIndex,
                             int tileSize) const;

  std::filesystem::path CacheDir;
  glm::vec3 Background;
  int DefaultTileSize;
  size_t MaxEntries;
  std::unordered_map<std::string, std::string> KeyToStem;
  std::unordered_map<std::string, TexturePixelData> StemPixels;
  std::list<std::string> LruOrder;
  std::unordered_map<std::string, std::list<std::string>::iterator> LruIt;
};

} // namespace cutum

#endif
