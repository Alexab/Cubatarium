#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"

namespace cutum
{
// Quiescent calling-thread boundary only: all borrowed Get* pointers and the
// registry name reference must be out of use. Explicit owning snapshots and
// the independent WorldGenContentPinScope remain valid. No foreign TLS access,
// global publication change, allocation or worker synchronization is needed.
inline void ReleaseCallingThreadCatalogReads() noexcept
{
  UWorldGenPack::ReleaseThreadRead();
  UObjectFeatureConfigStorage::ReleaseThreadRead();
  UWorldGenRefs::ReleaseThreadRead();
  UObjectLibrary::ReleaseThreadRead();
  UBlockDefinitionStorage::ReleaseThreadReads();
  UBlockRegistry::ReleaseThreadName();
}

class UCatalogThreadReadsScope
{
public:
  UCatalogThreadReadsScope() noexcept = default;
  ~UCatalogThreadReadsScope() {ReleaseCallingThreadCatalogReads();}
  UCatalogThreadReadsScope(const UCatalogThreadReadsScope &) = delete;
  UCatalogThreadReadsScope &operator=(const UCatalogThreadReadsScope &) = delete;
};
} // namespace cutum
