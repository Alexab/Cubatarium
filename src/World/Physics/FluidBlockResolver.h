#pragma once

#include "World/Blocks/IUFluidBlockResolver.h"

namespace cutum
{

class UBlockDefinitionStorage;

class UFluidBlockResolver : public IUFluidBlockResolver
{
public:
  explicit UFluidBlockResolver(const UBlockDefinitionStorage &definitions);

  BlockId ResolveFluidBlockId(const UBlockWorld &block_world,
                              glm::ivec3 block_pos) const override;
  BlockId ResolveFluidBlockIdForMesh(const IUChunkMeshReader &reader,
                                     glm::ivec3 block_pos) const override;
  FluidKind ResolveFluidKind(const UBlockWorld &block_world,
                             glm::ivec3 block_pos,
                             BlockId block_id) const override;

private:
  const UBlockDefinitionStorage &Definitions;
};

} // namespace cutum
