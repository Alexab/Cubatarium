#pragma once
#include "Render/Camera/CameraVolumeSweep.h"
#include "World/Core/World.h"
#include "World/Core/BlockWorld.h"
#include "World/Chunks/KnownAirTerrainSlice.h"
#include "Blocks/BlockRegistry.h"
#include <map>
#include <tuple>
namespace cutum {
inline float SweepCameraVolume(const UWorld &world, glm::vec3 start, glm::vec3 end, float radius) {
 const glm::ivec3 low=WorldPosToBlock(glm::min(start,end)-glm::vec3(radius));
 const glm::ivec3 high=WorldPosToBlock(glm::max(start,end)+glm::vec3(radius));
 std::map<std::tuple<int,int,int>,bool> missing;
 float fraction=1.f;
 for(int x=low.x;x<=high.x;++x)for(int y=low.y;y<=high.y;++y)for(int z=low.z;z<=high.z;++z) {
  const glm::ivec3 cell(x,y,z),chunk=UChunkManager::WorldToChunk(cell);
  const auto key=std::make_tuple(chunk.x,chunk.y,chunk.z);
  auto found=missing.find(key);
  if(found==missing.end())found=missing.emplace(key,!world.GetBlockWorld().GetChunkManager().HasChunk(chunk) && !IsKnownAirTerrainSlice(world,chunk)).first;
  if(!found->second && !world.GetBlockRegistry().BlocksMovement(world.GetBlockWorld().GetBlock(cell)))continue;
  // World objects are instantiated as individual solid voxels in BlockWorld.
  // No creature broadphase is consulted: bodies/held items/fluids cannot shorten the boom.
  const glm::vec3 center=BlockCenter(cell);
  fraction=std::min(fraction,SweepCameraAabb(start,end,center-glm::vec3(.5f),center+glm::vec3(.5f),radius));
 }
 return fraction;
}
}
