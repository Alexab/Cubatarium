#ifndef RESOURCEPACK_H
#define RESOURCEPACK_H

#include "Blocks/BlockDefinition.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

struct ResourcePackManifest
{
  std::string Id;
  std::string Name;
  std::string License;
  int Version{1};
  int Priority{0};
  int Resolution{16};
  std::filesystem::path Root;
};

struct ResourcePackBlock
{
  BlockDefinition Definition;
  std::array<std::string, 6> TextureStems{};
};

class UResourcePack
{
public:
  static std::optional<ResourcePackManifest>
  LoadManifest(const std::filesystem::path &root);

  static std::vector<ResourcePackBlock>
  LoadBlocks(const ResourcePackManifest &manifest);

  static std::filesystem::path
  TexturePath(const ResourcePackManifest &manifest, const std::string &stem);

  static void RegisterTextures(const ResourcePackManifest &manifest,
                               class UTextureBaseStorage &storage);
};

} // namespace cutum

#endif
