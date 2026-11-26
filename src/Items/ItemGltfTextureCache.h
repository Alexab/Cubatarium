#ifndef ITEM_GLTF_TEXTURE_CACHE_H
#define ITEM_GLTF_TEXTURE_CACHE_H

#include "Render/Textures/VisualTexture.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

/// Loads PNG textures referenced by item glTF (material name → OpenGL tex).
class ItemGltfTextureCache
{
public:
  static ItemGltfTextureCache &Instance();
  static void ClearForContextShutdown();
  static VisualTextureStorage InspectCurrentStorage();

  /// Absolute glTF path: inventory and worn variants can share a folder.
  GLuint Get(const std::filesystem::path &modelGltfAbs,
             const std::string &textureStem);

  /// Item-only albedo: sRGB decode precedes filtering; shared by image path.
  /// The legacy Get() representation remains unchanged for creature consumers.
  GLuint GetLinearAlbedo(const std::filesystem::path &imageAbs);

  void Clear();
  struct MemoryStats
  {
    uint64_t Textures{0}, RgbaMipBytes{0}, InvalidTextures{0};
    std::vector<std::array<int, 3>> Dimensions;
  };
  struct LookupStats
  {
    uint64_t FastHits{0}, CanonicalResolutions{0};
  };
  LookupStats GetLookupStats();
  /// Allocated uncompressed RGBA texels, including mip levels; not driver VRAM.
  MemoryStats InspectMemory(bool itemsOnly = false);

private:
  ItemGltfTextureCache();
  ~ItemGltfTextureCache();

  struct ModelTextures
  {
    std::unordered_map<std::string, size_t> Aliases;
    std::vector<VisualTexture> Images;
    void RetireUnreferencedImages();
  };
  static bool ReadModelImages(const std::filesystem::path &gltfPath, ModelTextures &out);
  static bool IndexImageDirectory(const std::filesystem::path &dir, ModelTextures &out);

  std::mutex Mu;
  /// Key: normalized full glTF path. Material tables belong to a model.
  std::unordered_map<std::string, ModelTextures> ByModel;
  std::unordered_map<std::string, VisualTexture> LinearAlbedo;
  std::unordered_map<std::string, const VisualTexture *> LinearAliases;
  LookupStats LookupCounters;
};

} // namespace cutum

#endif
