#ifndef CREATURE_SPAWN_PLACEMENT_H
#define CREATURE_SPAWN_PLACEMENT_H

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Horizontal distance from the viewer to the "spawn by view" spot: far
/// enough that the body (any yaw) clears the viewer, never closer than the
/// old fixed 3 blocks. A fixed 3 put the ogre's box against the player and
/// the fallback candidates then spawned it on top of them.
inline float SpawnAheadDistance(float viewerHalfWidth,
                                const glm::vec3 &sizeBlocks)
{
  constexpr float kMinAhead = 3.0f;
  constexpr float kMargin = 0.5f;
  const float bodyRadius = 0.5f * std::hypot(sizeBlocks.x, sizeBlocks.z);
  return std::max(kMinAhead, viewerHalfWidth + bodyRadius + kMargin);
}

/// Center coordinate on one axis so the footprint covers whole block cells
/// (blocks are centered on integers): an odd cell count centers on a block,
/// an even one on a block boundary. A 1.1-wide cow centered on a block
/// pokes 0.05 into both neighbouring columns.
inline float AlignSpawnAxis(float coord, float sizeBlocks)
{
  const int cells = std::max(1, static_cast<int>(std::ceil(sizeBlocks - 1e-3f)));
  if (cells % 2 == 1)
  {
    return std::round(coord);
  }
  return std::floor(coord) + 0.5f;
}

/// Spawn candidates around `base` (feet origin), nearest first: a square grid
/// of +-3 steps whose step grows with the footprint (1 block up to 3 blocks
/// wide, so a whale searches +-12 instead of +-3), each aligned to the block
/// grid, at 0, +1 and +2 blocks up. Candidates whose footprint would overlap
/// the viewer's (with a small margin) are dropped.
inline std::vector<glm::vec3>
BuildSizedSpawnCandidates(const glm::vec3 &base, const glm::vec3 &sizeBlocks,
                          const glm::vec3 &viewerOrigin,
                          const glm::vec3 &viewerSize)
{
  constexpr int kSteps = 3;
  constexpr float kViewerMargin = 0.25f;
  const float maxXZ = std::max(sizeBlocks.x, sizeBlocks.z);
  const float step = std::max(1.0f, std::ceil(maxXZ / 3.0f));

  struct Offset
  {
    int x;
    int z;
  };
  std::vector<Offset> offsets;
  for (int x = -kSteps; x <= kSteps; ++x)
  {
    for (int z = -kSteps; z <= kSteps; ++z)
    {
      offsets.push_back({x, z});
    }
  }
  std::stable_sort(offsets.begin(), offsets.end(),
                   [](const Offset &a, const Offset &b)
                   { return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z; });

  const auto overlapsViewer = [&](const glm::vec3 &origin)
  {
    const float gapX = std::abs(origin.x - viewerOrigin.x) -
                       0.5f * (sizeBlocks.x + viewerSize.x);
    const float gapZ = std::abs(origin.z - viewerOrigin.z) -
                       0.5f * (sizeBlocks.z + viewerSize.z);
    // XZ only: candidate heights are snapped to the ground afterwards.
    return gapX < kViewerMargin && gapZ < kViewerMargin;
  };

  std::vector<glm::vec3> out;
  out.reserve(offsets.size() * 3);
  for (const float lift : {0.0f, 1.0f, 2.0f})
  {
    for (const Offset &o : offsets)
    {
      const glm::vec3 candidate(
          AlignSpawnAxis(base.x + step * static_cast<float>(o.x), sizeBlocks.x),
          base.y + lift,
          AlignSpawnAxis(base.z + step * static_cast<float>(o.z), sizeBlocks.z));
      if (overlapsViewer(candidate) ||
          std::find(out.begin(), out.end(), candidate) != out.end())
      {
        continue;
      }
      out.push_back(candidate);
    }
  }
  return out;
}

} // namespace cutum

#endif
