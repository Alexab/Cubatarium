#include "ResourcePacks/BlockMergeRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Textures/TextureBase.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>

namespace cutum
{

namespace fs = std::filesystem;

std::string UBlockMergeRegistry::ResolveStem(
    const std::string &stem, const std::vector<ResourcePackManifest> &packs) const
{
  for (const auto &pack : packs)
  {
    const fs::path path = UResourcePack::TexturePath(pack, stem);
    if (fs::exists(path))
    {
      return stem;
    }
  }
  return {};
}

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
              return a.Priority < b.Priority;
            });

  for (const auto &pack : sorted)
  {
    for (const auto &block : UResourcePack::LoadBlocks(pack))
    {
      if (BlocksByName.find(block.Definition.Name) != BlocksByName.end())
      {
        continue;
      }
      MergedEntry entry;
      entry.Definition = block.Definition;
      entry.Stems = block.TextureStems;
      BlocksByName[block.Definition.Name] = entry;
    }
  }

  for (auto &pair : BlocksByName)
  {
    for (int face = 0; face < 6; ++face)
    {
      const std::string resolved =
          ResolveStem(pair.second.Stems[static_cast<size_t>(face)], sorted);
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

  for (const auto &rt : RuntimeOverlay)
  {
    if (BlocksByName.find(rt.first.Name) != BlocksByName.end())
    {
      continue;
    }
    MergedEntry entry;
    entry.Definition = rt.first;
    entry.Stems = rt.second;
    for (int face = 0; face < 6; ++face)
    {
      const std::string resolved =
          ResolveStem(entry.Stems[static_cast<size_t>(face)], sorted);
      if (!resolved.empty())
      {
        entry.Stems[static_cast<size_t>(face)] = resolved;
      }
      else if (PlaceholderCache)
      {
        entry.Stems[static_cast<size_t>(face)] =
            PlaceholderCache->GetOrCreateStem(entry.Definition.Name, face,
                                              PlaceholderTileSize);
      }
    }
    BlocksByName[rt.first.Name] = entry;
  }

  AssignRuntimeIds();
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

  MergedEntry entry;
  entry.Definition.Name = name;
  entry.Definition.Physics = BlockPhysicsProfile::Solid();
  entry.Definition.Types = {"unknown"};
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

BlockId UBlockMergeRegistry::ResolveName(const std::string &name)
{
  if (name.empty())
  {
    return BLOCK_AIR;
  }
  const auto it = NameToId.find(name);
  if (it != NameToId.end())
  {
    return it->second;
  }
  return CreateSyntheticBlock(name);
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
  if (PlaceholderCache)
  {
    Rebuild(ActivePacks, PlaceholderCache, PlaceholderTileSize);
  }
  return NameToId.count(def.Name) ? NameToId.at(def.Name) : BLOCK_AIR;
}

void UBlockMergeRegistry::UnregisterRuntimeBlock(const std::string &name)
{
  RuntimeOverlay.erase(
      std::remove_if(RuntimeOverlay.begin(), RuntimeOverlay.end(),
                     [&](const auto &p) { return p.first.Name == name; }),
      RuntimeOverlay.end());
  BlocksByName.erase(name);
  if (PlaceholderCache)
  {
    Rebuild(ActivePacks, PlaceholderCache, PlaceholderTileSize);
  }
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
