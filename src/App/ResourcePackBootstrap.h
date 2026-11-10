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

private:
  /// Identity of the pack set last applied in full (selection + resolved pack
  /// roots + manifest/dir timestamps). Re-applying the same set is a no-op, so
  /// creating or loading a world with unchanged packs does not rebuild the
  /// whole block catalog and every block texture again. Set
  /// CUBA_PACK_REAPPLY=1 to force a full rebuild (editing pack files live).
  std::string LastAppliedKey;
  std::string LastAppliedFingerprint;
};

} // namespace cutum

#endif
