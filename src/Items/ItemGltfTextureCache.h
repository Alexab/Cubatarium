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

  /// modelGltfAbs: absolute path to models/items/<id>/model.gltf
  GLuint Get(const std::filesystem::path &modelGltfAbs,
             const std::string &textureStem);

  void Clear();

private:
  ItemGltfTextureCache() = default;

  void EnsureLoaded(const std::filesystem::path &modelGltfAbs);

  std::mutex Mu;
  /// Key: model folder path (parent of model.gltf).
  std::unordered_map<std::string, std::unordered_map<std::string, GLuint>>
      ByModelDir;
};

} // namespace cutum

#endif
