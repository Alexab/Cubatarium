#include "ResourcePacks/BlockMergeRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Textures/TextureBase.h"
#include "ResourcePacks/BlockNameUtil.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>

namespace cutum
{

namespace fs = std::filesystem;

const std::unordered_set<std::string> UBlockMergeRegistry::kTierAWorldgenNames =
    {"bedrock",  "stone",      "dirt",        "grass",      "sand",
     "sandstone", "gravel",    "snow",        "clay",       "ice",
     "hellrock", "water",      "lava",        "fire",       "wood",
     "tree_log", "tree_leaves"};

void UBlockMergeRegistry::Rebuild(
    const std::vector<ResourcePackManifest> &packs,
    std::shared_ptr<UPlaceholderTextureCache> placeholder,
    int placeholderTileSize)
{
  ActivePacks = packs;
  PlaceholderCache = std::move(placeholder);
  PlaceholderTileSize = placeholderTileSize;
  BlocksByName.clear();
  NameToId.clear();
  IdToName.clear();
  CubeDescs.clear();

  std::vector<ResourcePackManifest> sorted = packs;
  std::sort(sorted.begin(), sorted.end(),
            [](const ResourcePackManifest &a, const ResourcePackManifest &b) {
              return a.EffectivePriority < b.EffectivePriority;
            });

  MergeBlockCatalog(sorted);
  ResolveBlockDefinitions(sorted);
  ApplyTextureOverrides(sorted);
  ResolveTextureAtlas(sorted);

  for (const auto &rt : RuntimeOverlay)
  {
    if (BlocksByName.find(rt.first.Name) != BlocksByName.end())
    {
      continue;
    }
    MergedEntry entry;
    entry.Definition = rt.first;
    entry.Stems = rt.second;
    entry.OwnerPackId = "runtime";
    BlocksByName[rt.first.Name] = entry;
  }
  if (!RuntimeOverlay.empty())
  {
    ResolveTextureAtlas(sorted);
  }

  AssignRuntimeIds();
  RuntimeOverlayDirty = false;
}

void UBlockMergeRegistry::MergeBlockCatalog(
    const std::vector<ResourcePackManifest> &sorted)
{
  struct PendingBlock
  {
    std::string RegistryName;
    std::string LocalName;
    ResourcePackBlock Block;
    ResourcePackManifest Pack;
  };
  std::vector<PendingBlock> pending;

  for (const auto &pack : sorted)
  {
    for (const auto &block : UResourcePack::LoadBlocks(pack))
    {
      PendingBlock pb;
      pb.LocalName = block.Definition.Name;
      pb.RegistryName = block.Definition.Name;
      pb.Block = block;
      pb.Pack = pack;
      if (pack.MergeMode == PackMergeMode::Duplicate)
      {
        pb.RegistryName = MakeQualifiedBlockName(pack.Id, pb.LocalName);
      }
      pending.push_back(std::move(pb));
    }
  }

  for (const auto &pb : pending)
  {
    const auto existing = BlocksByName.find(pb.RegistryName);
    if (existing != BlocksByName.end() && !pb.Pack.Id.empty() &&
        existing->second.OwnerPackId == pb.Pack.Id &&
        pb.RegistryName == pb.LocalName)
    {
      continue;
    }
    if (existing != BlocksByName.end())
    {
      if (pb.Pack.MergeMode == PackMergeMode::SkipExisting)
      {
        continue;
      }
      if (pb.Pack.MergeMode == PackMergeMode::Override)
      {
        MergedEntry entry;
        entry.Definition = pb.Block.Definition;
        entry.Definition.Name = pb.RegistryName;
        entry.Stems = pb.Block.TextureStems;
        entry.OwnerPackId = pb.Pack.Id;
        entry.IsQualifiedDuplicate =
            pb.RegistryName != pb.LocalName;
        BlocksByName[pb.RegistryName] = entry;
        continue;
      }
      if (pb.Pack.MergeMode == PackMergeMode::Duplicate)
      {
        if (BlocksByName.find(pb.RegistryName) != BlocksByName.end())
        {
          continue;
        }
      }
    }
    if (BlocksByName.find(pb.RegistryName) != BlocksByName.end())
    {
      continue;
    }
    MergedEntry entry;
    entry.Definition = pb.Block.Definition;
    entry.Definition.Name = pb.RegistryName;
    entry.Stems = pb.Block.TextureStems;
    entry.OwnerPackId = pb.Pack.Id;
    entry.IsQualifiedDuplicate = pb.RegistryName != pb.LocalName;
    BlocksByName[pb.RegistryName] = entry;
  }
}

void UBlockMergeRegistry::ResolveBlockDefinitions(
    const std::vector<ResourcePackManifest> & /*sorted*/)
{
  for (auto &pair : BlocksByName)
  {
    pair.second.Definition.Name = pair.first;
  }
}

void UBlockMergeRegistry::ApplyTextureOverrides(
    const std::vector<ResourcePackManifest> &sorted)
{
  for (const auto &pack : sorted)
  {
    const auto overrides = UResourcePack::LoadTextureOverrideMap(pack);
    for (const auto &[blockName, faceOverrides] : overrides)
    {
      auto it = BlocksByName.find(blockName);
      if (it == BlocksByName.end())
      {
        continue;
      }
      for (const auto &ov : faceOverrides)
      {
        for (int face : ov.Faces)
        {
          if (face >= 0 && face < 6)
          {
            it->second.Stems[static_cast<size_t>(face)] = ov.Stem;
          }
        }
      }
    }
  }
}

std::string UBlockMergeRegistry::ResolveStemInPack(
    const std::string &stem, const ResourcePackManifest &pack) const
{
  if (stem.empty())
  {
    return {};
  }
  if (stem.find('/') != std::string::npos)
  {
    return stem;
  }
  const fs::path path = UResourcePack::TexturePath(pack, stem);
  if (fs::exists(path))
  {
    return PackQualifiedTextureStem(pack.Id, stem);
  }
  return {};
}

void UBlockMergeRegistry::ResolveTextureAtlas(
    const std::vector<ResourcePackManifest> &sorted)
{
  std::unordered_map<std::string, ResourcePackManifest> packById;
  for (const auto &pack : sorted)
  {
    packById[pack.Id] = pack;
  }

  for (auto &pair : BlocksByName)
  {
    const auto packIt = packById.find(pair.second.OwnerPackId);
    if (packIt == packById.end())
    {
      continue;
    }
    const ResourcePackManifest &ownerPack = packIt->second;
    for (int face = 0; face < 6; ++face)
    {
      const std::string resolved = ResolveStemInPack(
          pair.second.Stems[static_cast<size_t>(face)], ownerPack);
      if (!resolved.empty())
      {
        pair.second.Stems[static_cast<size_t>(face)] = resolved;
      }
      else if (PlaceholderCache)
      {
        pair.second.Stems[static_cast<size_t>(face)] =
            PlaceholderCache->GetOrCreateStem(pair.first, face,
                                              PlaceholderTileSize);
      }
    }
  }
}

void UBlockMergeRegistry::AssignRuntimeIds()
{
  NameToId.clear();
  IdToName.clear();
  CubeDescs.clear();

  std::set<std::string> names;
  for (const auto &pair : BlocksByName)
  {
    names.insert(pair.first);
  }

  BlockId nextId = 1;
  for (const auto &name : names)
  {
    const auto it = BlocksByName.find(name);
    if (it == BlocksByName.end())
    {
      continue;
    }
    it->second.Definition.Id = nextId;
    NameToId[name] = nextId;
    IdToName[nextId] = name;

    MergedCubeDesc desc;
    desc.Name = name;
    desc.Id = nextId;
    desc.Stems = it->second.Stems;
    desc.AnimFrames = std::max(1, it->second.Definition.Animation.FrameCount);
    CubeDescs.push_back(desc);
    ++nextId;
  }
}

BlockId UBlockMergeRegistry::CreateSyntheticBlock(const std::string &name)
{
  if (name.empty() || IsReservedBlockName(name))
  {
    return BLOCK_AIR;
  }
  if (NameToId.count(name))
  {
    return NameToId.at(name);
  }

  const std::string local = LocalBlockName(name);
  if (kTierAWorldgenNames.count(local) != 0)
  {
    std::cerr << "BlockMergeRegistry: CRITICAL missing Tier A block '" << name
              << "'" << std::endl;
    return BLOCK_AIR;
  }

  MergedEntry entry;
  entry.Definition.Name = name;
  entry.Definition.Physics = BlockPhysicsProfile::Solid();
  entry.Definition.Types = {"unknown"};
  entry.OwnerPackId = "synthetic";
  for (int face = 0; face < 6; ++face)
  {
    if (PlaceholderCache)
    {
      entry.Stems[static_cast<size_t>(face)] =
          PlaceholderCache->GetOrCreateStem(name, face, PlaceholderTileSize);
    }
  }
  BlocksByName[name] = entry;
  AssignRuntimeIds();
  return NameToId.at(name);
}

BlockId UBlockMergeRegistry::ResolveBlockName(const std::string &name)
{
  if (name.empty())
  {
    return BLOCK_AIR;
  }
  if (!WorldgenOwnerPackId.empty() && !IsQualifiedBlockName(name))
  {
    const std::string qualified =
        MakeQualifiedBlockName(WorldgenOwnerPackId, name);
    const auto qualifiedIt = NameToId.find(qualified);
    if (qualifiedIt != NameToId.end())
    {
      return qualifiedIt->second;
    }
  }
  const auto it = NameToId.find(name);
  if (it != NameToId.end())
  {
    return it->second;
  }
  if (IsQualifiedBlockName(name))
  {
    const std::string local = LocalBlockName(name);
    const auto localIt = NameToId.find(local);
    if (localIt != NameToId.end())
    {
      return localIt->second;
    }
  }
  return CreateSyntheticBlock(name);
}

BlockId UBlockMergeRegistry::ResolveName(const std::string &name)
{
  return ResolveBlockName(name);
}

BlockId UBlockMergeRegistry::RegisterRuntimeBlock(
    const BlockDefinition &def, const std::array<std::string, 6> &stems)
{
  if (def.Name.empty() || IsReservedBlockName(def.Name))
  {
    return BLOCK_AIR;
  }
  RuntimeOverlay.erase(
      std::remove_if(RuntimeOverlay.begin(), RuntimeOverlay.end(),
                     [&](const auto &p) { return p.first.Name == def.Name; }),
      RuntimeOverlay.end());
  RuntimeOverlay.push_back({def, stems});
  RuntimeOverlayDirty = true;
  if (NameToId.count(def.Name))
  {
    return NameToId.at(def.Name);
  }
  return BLOCK_AIR;
}

void UBlockMergeRegistry::FlushRuntimeOverlay()
{
  if (!RuntimeOverlayDirty || !PlaceholderCache)
  {
    return;
  }
  Rebuild(ActivePacks, PlaceholderCache, PlaceholderTileSize);
  RuntimeOverlayDirty = false;
}

void UBlockMergeRegistry::UnregisterRuntimeBlock(const std::string &name)
{
  RuntimeOverlay.erase(
      std::remove_if(RuntimeOverlay.begin(), RuntimeOverlay.end(),
                     [&](const auto &p) { return p.first.Name == name; }),
      RuntimeOverlay.end());
  BlocksByName.erase(name);
  RuntimeOverlayDirty = true;
}

void UBlockMergeRegistry::PopulateBlockDefinitionStorage(
    UBlockDefinitionStorage &out) const
{
  std::unordered_map<BlockId, BlockDefinition> byId;
  for (const auto &pair : BlocksByName)
  {
    const BlockId id = NameToId.at(pair.first);
    byId[id] = pair.second.Definition;
    byId[id].Id = id;
  }
  out.ReplaceAll(std::move(byId), NameToId);
}

void UBlockMergeRegistry::PopulateTextureBaseStorage(
    UTextureBaseStorage &out) const
{
  out.Clear();
  for (const auto &pack : ActivePacks)
  {
    UResourcePack::RegisterTextures(pack, out);
  }
  if (PlaceholderCache)
  {
    PlaceholderCache->RegisterAll(out);
  }
}

} // namespace cutum
