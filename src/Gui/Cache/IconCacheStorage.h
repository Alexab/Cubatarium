#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include "Render/Textures/GlTextureOwner.h"
#include <unordered_map>

namespace cutum
{

struct IconTextureTableStorage
{
  size_t cpuBytes{}, owned{}, borrowed{}, ownedTexels{};
  bool supported{};
};

inline IconTextureTableStorage InspectIconTextureTable(
    const std::unordered_map<std::string, UGlTextureOwner> &icons, size_t texelsPerIcon) noexcept
{
  StlSourceStorage table, texels;
  table.Hash(icons);
  IconTextureTableStorage out;
  for (const auto &[name, texture] : icons)
  {
    table.Text(name);
    table.bytes.supported &= texture.Get() != 0;
    if (texture.IsOwned())
    {
      ++out.owned;
      texels.Add(texelsPerIcon);
    }
    else if (texture.Get())
      ++out.borrowed;
  }
  out.cpuBytes = table.bytes.requested;
  out.ownedTexels = texels.bytes.requested;
  out.supported = table.bytes.supported && texels.bytes.supported;
  return out;
}

struct CreatureIconCacheStorage
{
  IconTextureTableStorage species, skins;
  size_t cacheBytes{}, queueBytes{}, cpuBytes{}, pending{};
  bool present{}, supported{};
};

struct ItemIconCacheStorage
{
  IconTextureTableStorage icons;
  size_t cacheBytes{}, queueBytes{}, fingerprintBytes{}, stateBytes{}, cpuBytes{};
  size_t pending{}, fingerprints{}, stateRequests{};
  bool present{}, supported{};
};

} // namespace cutum
