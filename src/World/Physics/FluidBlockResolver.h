#pragma once

#include "World/Blocks/IUFluidBlockResolver.h"

namespace cutum
{

class UBlockDefinitionStorage;
class UBlockWorld;

class UFluidBlockResolver : public IUFluidBlockResolver
{
public:
  explicit UFluidBlockResolver(const UBlockDefinitionStorage &definitions);

  static BlockId ResolveWaterBlockId(
      const UBlockDefinitionStorage &definitions);
  static FluidKind FluidKindFromBlockId(
      const UBlockDefinitionStorage &definitions, BlockId id);
  static BlockId BlockIdFromFluidKind(
      const UBlockDefinitionStorage &definitions, FluidKind kind);
  static BlockId ResolveFluidBlockId(const UBlockWorld &block_world,
                                     const UBlockDefinitionStorage &definitions,
                                     glm::ivec3 block_pos);
  static BlockId ResolveFluidKind(const UBlockWorld &block_world,
                                  const UBlockDefinitionStorage &definitions,
                                  glm::ivec3 block_pos, BlockId block_id);
  static BlockId ResolveFluidBlockIdForMesh(
      const IUChunkMeshReader &reader,
      const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos);

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
