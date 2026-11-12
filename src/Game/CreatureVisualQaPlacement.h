#ifndef CREATURE_VISUAL_QA_PLACEMENT_H
#define CREATURE_VISUAL_QA_PLACEMENT_H

#include "Blocks/BlockRegistry.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Environment/CreatureSpawnPlacement.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace cutum
{
inline std::optional<glm::vec3> FindCreatureQaLandPosition(
    UWorld &world, const glm::vec3 &target, const CreatureDefinition &definition)
{
  std::vector<glm::ivec2> offsets;
  for (int z = -32; z <= 32; z += 2)
  for (int x = -32; x <= 32; x += 2) offsets.emplace_back(x,z);
  std::stable_sort(offsets.begin(),offsets.end(),[](const auto &a,const auto &b)
  { return a.x*a.x+a.y*a.y < b.x*b.x+b.y*b.y; });
  float bestDistance = 1e9f;
  std::optional<glm::vec3> best;
  const auto &size = definition.bounds.restSizeBlocks;
  for (const auto &offset : offsets)
  {
    // Alignment moves each axis by at most half a block. Once even that
    // lower bound is farther than the best site, later offsets cannot win.
    const float nearest = std::max(0.f,std::hypot(static_cast<float>(offset.x),
                                                 static_cast<float>(offset.y))-std::sqrt(.5f));
    if (nearest*nearest > bestDistance) break;
    const glm::vec3 probe(AlignSpawnAxis(target.x+offset.x,size.x),target.y,
                          AlignSpawnAxis(target.z+offset.y,size.z));
    const auto candidate = AdjustSpawnBodyOrigin(world,definition,probe);
    const float distance = glm::dot(candidate-target,candidate-target);
    if (distance >= bestDistance || !CanSpawnCreatureAt(world,definition,candidate)) continue;
    bestDistance = distance;
    best = candidate;
  }
  return best;
}

inline std::optional<glm::vec3> FindCreatureQaWaterPosition(
    UWorld &world, const glm::vec3 &target,
    const std::vector<glm::vec3> &occupied = {},
    const glm::vec3 &size = glm::vec3(0.f),
    const CreatureDefinition *definition = nullptr)
{
  const auto &bw = world.GetBlockWorld();
  auto &reg = world.GetBlockRegistry();
  const int sea = world.GetProceduralSettings().SeaLevel;
  float bestDistance = 1e9f;
  std::optional<glm::vec3> best;
  for (int dz = -64; dz <= 64; dz += 2)
  for (int dx = -64; dx <= 64; dx += 2)
  {
    const int x = static_cast<int>(std::floor(target.x)) + dx;
    const int z = static_cast<int>(std::floor(target.z)) + dz;
    // Match aquatic spawn's 75% immersion rule: a tall sea-serpent neck may
    // project into open air, while the entire body must still clear solids.
    glm::vec3 candidate(x, sea - std::max(3.f, size.y*.75f+.25f), z);
    if (!reg.IsLiquid(bw.GetBlock(glm::ivec3(x, sea - 1, z)))) continue;
    // SpawnCreature chooses the column's mid-depth itself. Validate that
    // actual adjusted origin rather than a requested Y it will overwrite.
    if (definition) candidate = AdjustSpawnBodyOrigin(world,*definition,candidate);
    const float distance = glm::dot(candidate - target, candidate - target);
    if (distance >= bestDistance) continue;
    // Tiny species also need an open-water review site: a collider-sized
    // pocket beside a shore wall cannot accommodate a visible camera ray.
    const int radius = std::max(6,static_cast<int>(std::ceil(std::hypot(size.x,size.z)*.5f+.5f)));
    if (std::any_of(occupied.begin(), occupied.end(),
        [&](const glm::vec3 &p) { return glm::distance(p, candidate) < std::max(3.f,2.f*radius); }))
      continue;
    if (!reg.IsLiquid(bw.GetBlock(glm::ivec3(x, sea - 1, z))) ||
        !reg.IsLiquid(bw.GetBlock(glm::ivec3(x, sea - 3, z))) ||
        bw.GetBlock(glm::ivec3(x, sea + 1, z)) != BLOCK_AIR)
      continue;
    // Check the full yaw envelope, not one water column. Large whales failed
    // spawning on the old shallow shore site despite native previews working.
    bool fits = true;
    // Blocks are centred on integers, not on integer + .5 along Y.
    const int bottom = static_cast<int>(std::ceil(candidate.y-1.5f));
    const int top = static_cast<int>(std::floor(candidate.y+std::max(1.f,size.y)+.5f));
    for (int oz = -radius; oz <= radius && fits; ++oz)
    for (int ox = -radius; ox <= radius && fits; ++ox)
    {
      if (ox*ox+oz*oz > radius*radius) continue;
      for (int y = bottom; y <= top; ++y)
      {
        const auto block = bw.GetBlock(glm::ivec3(x+ox,y,z+oz));
        if (!reg.IsLiquid(block) && !(y >= sea && block == BLOCK_AIR))
        { fits = false; break; }
      }
    }
    if (!fits) continue;
    if (definition && !CanSpawnCreatureAt(world,*definition,candidate)) continue;
    if (distance < bestDistance) { bestDistance = distance; best = candidate; }
  }
  return best;
}
} // namespace cutum
#endif
