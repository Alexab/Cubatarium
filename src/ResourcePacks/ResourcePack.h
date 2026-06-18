#ifndef RESOURCEPACK_H
#define RESOURCEPACK_H

#include "Blocks/BlockDefinition.h"
#include "ResourcePacks/TextureOverrides.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

enum class WorldgenRole
{
  Primary,
  Secondary
};

enum class PackMergeMode
{
  SkipExisting,
  Override,
  Duplicate
};

struct ResourcePackManifest
{
  std::string Id;
  std::string Name;
  std::string License;
  int Version{1};
  int PackFormat{1};
  int Priority{0};
  int Resolution{16};
  std::string MinGameVersion;
  std::vector<std::string> Depends;
  std::vector<std::string> Conflicts;
  bool AllowResolutionMix{false};
  WorldgenRole Role{WorldgenRole::Secondary};
  PackMergeMode MergeMode{PackMergeMode::SkipExisting};
  std::filesystem::path Root;
  int EffectivePriority{0};
  int SelectionIndex{0};
};

struct ResourcePackBlock
{
  BlockDefinition Definition;
  std::array<std::string, 6> TextureStems{};
};

/// Runtime texture atlas key: isolates same stem name across packs (e.g. stone).
std::string PackQualifiedTextureStem(const std::string &packId,
                                     const std::string &stem);

class UResourcePack
{
public:
  static std::optional<ResourcePackManifest>
  LoadManifest(const std::filesystem::path &root);

  static std::vector<ResourcePackBlock>
  LoadBlocks(const ResourcePackManifest &manifest);

  static std::filesystem::path
  TexturePath(const ResourcePackManifest &manifest, const std::string &stem);

  static TextureOverrideMap
  LoadTextureOverrideMap(const ResourcePackManifest &manifest);

  static void RegisterTextures(const ResourcePackManifest &manifest,
                               class UTextureBaseStorage &storage);
};

} // namespace cutum

#endif
