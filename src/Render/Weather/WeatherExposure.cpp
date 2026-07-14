#include "Render/Weather/WeatherExposure.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"

#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kSkyScanMaxBlocks = 64;
constexpr float kMinSkyExposureForPrecipitation = 0.12f;

bool IsSkyTransparent(const UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return true;
  }
  if (registry.IsLiquid(id))
  {
    return true;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Cross)
  {
    return true;
  }
  return !registry.BlocksMovement(id);
}

float SampleSkyExposureAtBlock(const UBlockWorld &world,
                               const UBlockRegistry &registry,
                               glm::ivec3 pos)
{
  for (int step = 1; step <= kSkyScanMaxBlocks; ++step)
  {
    const glm::ivec3 probe(pos.x, pos.y + step, pos.z);
    if (!IsSkyTransparent(registry, world.GetBlock(probe)))
    {
      const glm::ivec2 offsets[] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                                    {1, 1},  {-1, 1}, {1, -1}, {-1, -1}};
      float best = 0.0f;
      for (int radius = 1; radius <= 6; ++radius)
      {
        for (const glm::ivec2 &offset : offsets)
        {
          bool open = true;
          const glm::ivec3 sample(pos.x + offset.x * radius, pos.y,
                                  pos.z + offset.y * radius);
          for (int scan = 1; scan <= kSkyScanMaxBlocks; ++scan)
          {
            if (!IsSkyTransparent(
                    registry, world.GetBlock(sample + glm::ivec3(0, scan, 0))))
            {
              open = false;
              break;
            }
          }
          if (open)
          {
            best = std::max(best, std::max(0.0f, 1.0f - radius * 0.14f));
          }
        }
      }
      return best;
    }
  }
  return 1.0f;
}

} // namespace

float SampleSkyExposure01(const UWorld &world, const glm::vec3 &eye)
{
  if (!world.IsBlockWorldReady())
  {
    return 1.0f;
  }
  const glm::ivec3 block_pos(WorldCoordToBlockIndex(eye.x),
                             WorldCoordToBlockIndex(eye.y),
                             WorldCoordToBlockIndex(eye.z));
  return SampleSkyExposureAtBlock(world.GetBlockWorld(), world.GetBlockRegistry(),
                                  block_pos);
}

bool CanReceiveOutdoorPrecipitation(const UWorld &world, const glm::vec3 &eye)
{
  if (world.IsCameraInsideFluid(eye))
  {
    return false;
  }
  return SampleSkyExposure01(world, eye) >= kMinSkyExposureForPrecipitation;
}

} // namespace cutum
