#ifndef RESOURCEPACKBOOTSTRAP_H
#define RESOURCEPACKBOOTSTRAP_H

#include "Blocks/BlockDefinition.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <array>
#include <string>
#include <vector>

namespace cutum
{

class UCore;

/// Resource-pack application and runtime block overlay (extracted from UCore).
class UResourcePackBootstrap
{
public:
  void InitPlaceholderCache(UCore &core);

  bool ApplyResourcePacks(UCore &core,
                          const std::vector<std::string> &enabledIds);
  bool ApplyResourcePacks(UCore &core, const ResourcePackSelection &selection);
  void ApplyResourcePacksAfterWorldDataLoaded(UCore &core);
  void ReloadCreatureCatalog(UCore &core,
                             const std::vector<ResourcePackManifest> &packs);
  void RebuildBlockTexturesFromMergeRegistry(UCore &core);
  void PatchRuntimeBlockTextures(UCore &core,
                                 const RuntimeOverlayFlushResult &flush);

  bool RegisterRuntimeBlock(UCore &core, const BlockDefinition &def,
                            const std::array<std::string, 6> &textureStems);
  void BeginRuntimeBlockBatch(UCore &core);
  void EndRuntimeBlockBatch(UCore &core);
  void FlushRuntimeBlockOverlay(UCore &core);

  std::vector<std::string>
  NormalizeEnabledPackIds(const UCore &core,
                          const std::vector<std::string> &requested) const;
};

} // namespace cutum

#endif
