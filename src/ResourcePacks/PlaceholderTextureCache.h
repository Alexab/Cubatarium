#ifndef PLACEHOLDERTEXTURECACHE_H
#define PLACEHOLDERTEXTURECACHE_H

#include "Render/Textures/TextureBase.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cutum
{

class UPlaceholderTextureCache
{
public:
  UPlaceholderTextureCache(std::filesystem::path cacheDir, int tileSize,
                           glm::vec3 background, size_t maxEntries = 256);
  UPlaceholderTextureCache(const UPlaceholderTextureCache &) = delete;
  UPlaceholderTextureCache &operator=(const UPlaceholderTextureCache &) = delete;

  std::string GetOrCreateStem(const std::string &blockName, int faceIndex,
                              int tileSize);
  void RegisterAll(UTextureBaseStorage &storage) const;
  size_t GetEntryCount() const { return LruIt.size(); }
  size_t GetMaxEntries() const { return MaxEntries; }

  // Caller excludes cache creation/eviction during this diagnostic walk.
  template<class Inspector> void InspectStorage(Inspector &out) const {
    out.Path(CacheDir);out.Map(StemPixels);out.Map(LruIt);out.List(LruOrder);
    for(const auto &entry:StemPixels) {out.Text(entry.first);out.Vector(entry.second.Rgba);}
    for(const auto &entry:LruOrder) {out.Text(entry.first);out.Text(entry.second);}
  }

private:
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
  std::unordered_map<std::string, TexturePixelData> StemPixels;
  // The LRU node owns the key and stem. The lookup index borrows the same
  // immutable key, and each view is erased before its list node.
  using LruEntries = std::list<std::pair<std::string, std::string>>;
  LruEntries LruOrder;
  std::unordered_map<std::string_view, LruEntries::iterator> LruIt;
};

} // namespace cutum

#endif
