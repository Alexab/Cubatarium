#ifndef ITEM_GLTF_TEXTURE_CACHE_H
#define ITEM_GLTF_TEXTURE_CACHE_H

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

typedef unsigned int GLuint;

namespace cutum
{

/// Loads PNG textures referenced by item glTF (material name → OpenGL tex).
class ItemGltfTextureCache
{
public:
  static ItemGltfTextureCache &Instance();

  /// Absolute glTF path: inventory and worn variants can share a folder.
  GLuint Get(const std::filesystem::path &modelGltfAbs,
             const std::string &textureStem);

  void Clear();

private:
  ItemGltfTextureCache() = default;

  void EnsureLoaded(const std::filesystem::path &modelGltfAbs);

  std::mutex Mu;
  /// Key: normalized full glTF path. Material tables belong to a model.
  std::unordered_map<std::string, std::unordered_map<std::string, GLuint>>
      ByModel;
};

} // namespace cutum

#endif
