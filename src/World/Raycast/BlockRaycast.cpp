#include "World/Raycast/BlockRaycast.h"
#include <array>

#include "Blocks/BlockRegistry.h"

#include "World/Collision/VoxelDdaTraversal.h"

#include "World/Core/BlockWorld.h"

#include "World/Math/GridMath.h"

#include <cmath>

#include <limits>



namespace cutum

{



namespace

{



constexpr float kHalfBlock = 0.5f;



float NextBoundaryT(const glm::vec3 &origin, const glm::vec3 &direction,

                    int blockCoord, int axis)

{

  if (direction[axis] > 0.0f)

  {

    return (static_cast<float>(blockCoord) + kHalfBlock - origin[axis]) /

           direction[axis];

  }

  if (direction[axis] < 0.0f)

  {

    return (static_cast<float>(blockCoord) - kHalfBlock - origin[axis]) /

           direction[axis];

  }

  return std::numeric_limits<float>::max();

}



bool IsRaycastTarget(const UBlockWorld &world, const UBlockRegistry &registry,

                     glm::ivec3 pos)

{

  const BlockId Id = world.GetBlock(pos);

  return registry.BlocksMovement(Id);

}



bool IsAirPocketCell(const UBlockWorld &world, const UBlockRegistry &registry,

                     glm::ivec3 cell)

{

  if (!world.IsAir(cell))

  {

    return false;

  }

  static constexpr std::array<glm::ivec3, 6> kNeighbors = {

      glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),

      glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};

  for (const glm::ivec3 &offset : kNeighbors)

  {

    if (IsRaycastTarget(world, registry, cell + offset))

    {

      return true;

    }

  }

  return false;

}



glm::ivec3 InferFaceNormal(const UBlockWorld &world,

                           const UBlockRegistry &registry, glm::ivec3 air_cell,

                           glm::ivec3 solid_hint)

{

  const glm::ivec3 delta = air_cell - solid_hint;

  if (delta != glm::ivec3(0))

  {

    return delta;

  }

  static constexpr std::array<glm::ivec3, 6> kNeighbors = {

      glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),

      glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};

  for (const glm::ivec3 &offset : kNeighbors)

  {

    if (IsRaycastTarget(world, registry, air_cell + offset))

    {

      return -offset;

    }

  }

  return glm::ivec3(0, -1, 0);

}



} // namespace



std::optional<BlockRayHit>

RaycastSolidBlocks(const UBlockWorld &world, const UBlockRegistry &registry,

                   glm::vec3 origin, glm::vec3 direction, float maxDistance)

{

  const float len = glm::length(direction);

  if (len < 1e-6f)

  {

    return std::nullopt;

  }

  direction /= len;



  const float eps = 1e-4f;

  glm::ivec3 current = WorldPosToBlock(origin);



  if (IsRaycastTarget(world, registry, current))

  {

    BlockRayHit hit;

    hit.blockPos = current;

    hit.faceNormal =

        glm::ivec3(direction.x > 0.0f ? -1 : (direction.x < 0.0f ? 1 : 0),

                   direction.y > 0.0f ? -1 : (direction.y < 0.0f ? 1 : 0),

                   direction.z > 0.0f ? -1 : (direction.z < 0.0f ? 1 : 0));

    hit.distance = 0.0f;

    return hit;

  }



  float t = 0.0f;

  while (t < maxDistance)

  {

    const glm::vec3 pos = origin + direction * t;



    float tNext = maxDistance;

    int stepAxis = -1;

    const float tx = NextBoundaryT(origin, direction, current.x, 0);

    const float ty = NextBoundaryT(origin, direction, current.y, 1);

    const float tz = NextBoundaryT(origin, direction, current.z, 2);



    if (tx < tNext)

    {

      tNext = tx;

      stepAxis = 0;

    }

    if (ty < tNext)

    {

      tNext = ty;

      stepAxis = 1;

    }

    if (tz < tNext)

    {

      tNext = tz;

      stepAxis = 2;

    }



    if (tNext >= maxDistance)

    {

      break;

    }



    t = tNext + eps;

    glm::ivec3 next = current;

    if (stepAxis == 0)

    {

      next.x += (direction.x > 0.0f) ? 1 : -1;

    }

    else if (stepAxis == 1)

    {

      next.y += (direction.y > 0.0f) ? 1 : -1;

    }

    else if (stepAxis == 2)

    {

      next.z += (direction.z > 0.0f) ? 1 : -1;

    }



    if (IsRaycastTarget(world, registry, next))

    {

      BlockRayHit hit;

      hit.blockPos = next;

      hit.distance = t;

      hit.faceNormal = current - next;

      return hit;

    }



    current = next;

  }



  return std::nullopt;

}



std::optional<FluidPlacementHit> RaycastFluidPlacementTarget(

    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,

    glm::vec3 front, float max_distance)

{

  const float len = glm::length(front);

  if (len < 1e-6f)

  {

    return std::nullopt;

  }

  const glm::vec3 direction = front / len;



  const auto hit =

      RaycastSolidBlocks(world, registry, eye_pos, direction, max_distance);

  if (!hit)

  {

    return std::nullopt;

  }



  glm::ivec3 normal = hit->faceNormal;

  if (normal == glm::ivec3(0))

  {

    normal = InferFaceNormal(world, registry, hit->blockPos + glm::ivec3(0, 1, 0),

                             hit->blockPos);

  }



  std::optional<FluidPlacementHit> best;

  float best_distance = std::numeric_limits<float>::max();



  auto consider = [&](glm::ivec3 place_pos, glm::ivec3 face_normal,

                      bool via_fluid_volume, float distance)

  {

    if (!world.IsAir(place_pos))

    {

      return;

    }

    if (distance < best_distance)

    {

      best_distance = distance;

      FluidPlacementHit placement;

      placement.block_pos = place_pos;

      placement.face_normal = face_normal;

      placement.via_fluid_volume = via_fluid_volume;

      best = placement;

    }

  };



  const glm::ivec3 place_pos = hit->blockPos + normal;

  consider(place_pos, normal, false, hit->distance);



  TraverseVoxelRay(eye_pos, direction, std::max(0.0f, hit->distance - 0.05f),

                   [&](glm::ivec3 cell)

                   {

                     if (!IsAirPocketCell(world, registry, cell))

                     {

                       return false;

                     }

                     const float distance =

                         glm::length(BlockCenter(cell) - eye_pos);

                     const glm::ivec3 face_normal =

                         InferFaceNormal(world, registry, cell, hit->blockPos);

                     consider(cell, face_normal, true, distance);

                     return false;

                   });



  return best;

}



} // namespace cutum


