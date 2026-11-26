#ifndef CREATURETEXTURESTORAGE_H
#define CREATURETEXTURESTORAGE_H

#include "Render/Textures/VisualTexture.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureTextureStorage
{
public:
  UCreatureTextureStorage() = default;
  ~UCreatureTextureStorage();
  UCreatureTextureStorage(const UCreatureTextureStorage &) = delete;
  UCreatureTextureStorage &operator=(const UCreatureTextureStorage &) = delete;
  static std::shared_ptr<UCreatureTextureStorage> Create();
  bool LoadFromCreatureAndSkinRoots(const std::string &creaturesRoot, const std::string &skinsRoot,
                                   const std::vector<std::filesystem::path> &overlays = {});
  bool MergeCreatureRoot(const std::string &creaturesRoot);
  bool MergeSkinRoot(const std::string &skinsRoot);
  GLuint GetTexture(const std::string &assetKey) const;
  size_t Count() const { return Textures.size(); }
  void Clear() noexcept;
  void SwapContents(UCreatureTextureStorage &other) noexcept { Textures.swap(other.Textures); }
  VisualTextureStorage InspectOwnedStorage() const noexcept;
  void InspectRenderImages(class RenderImageCensus &out) const;

private:
  using TextureTable = std::unordered_map<std::string, VisualTexture>;
  static bool IndexRoot(const std::filesystem::path &root, bool skins, TextureTable &out);
  bool MergeRoot(const std::string &root, bool skins);
  TextureTable Textures;
  bool FactoryOwner{};
};

} // namespace cutum

#endif
