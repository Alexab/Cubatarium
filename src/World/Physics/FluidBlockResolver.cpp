#include "World/Physics/FluidBlockResolver.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Physics/FluidSpreadSystem.h"

namespace cutum
{

UFluidBlockResolver::UFluidBlockResolver(
    const UBlockDefinitionStorage &definitions)
    : Definitions(definitions)
{
}

BlockId UFluidBlockResolver::ResolveFluidBlockId(
    const UBlockWorld &block_world, glm::ivec3 block_pos) const
{
  return UFluidSpreadSystem::ResolveFluidBlockId(block_world, Definitions,
                                                 block_pos);
}

BlockId UFluidBlockResolver::ResolveFluidBlockIdForMesh(
    const IUChunkMeshReader &reader, glm::ivec3 block_pos) const
{
  return UFluidSpreadSystem::ResolveFluidBlockIdForMesh(reader, Definitions,
                                                      block_pos);
}

FluidKind UFluidBlockResolver::ResolveFluidKind(const UBlockWorld &block_world,
                                                glm::ivec3 block_pos,
                                                BlockId block_id) const
{
  (void)block_world;
  (void)block_pos;
  return UFluidSpreadSystem::FluidKindFromBlockId(Definitions, block_id);
}

} // namespace cutum
