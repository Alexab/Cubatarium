#include "World/Collision/VoxelDdaTraversal.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum
{

bool TraverseVoxelRay(const glm::vec3 &origin, const glm::vec3 &dir, float maxDist,
                      const std::function<bool(glm::ivec3)> &visitCell)
{
  if (glm::length(dir) < 1e-6f || maxDist <= 0.0f)
  {
    return false;
  }
  const glm::vec3 rd = glm::normalize(dir);
  glm::ivec3 cell(static_cast<int>(std::floor(origin.x)),
                  static_cast<int>(std::floor(origin.y)),
                  static_cast<int>(std::floor(origin.z)));

  const auto axisStep = [](float component)
  {
    if (std::abs(component) < 1e-6f)
    {
      return 0;
    }
    return component > 0.0f ? 1 : -1;
  };
  const glm::ivec3 step(axisStep(rd.x), axisStep(rd.y), axisStep(rd.z));
  const auto boundary = [&](float o, int c, int st)
  {
    if (st == 0)
    {
      return o;
    }
    return st > 0 ? static_cast<float>(c + 1) : static_cast<float>(c);
  };
  const auto safeInv = [](float v)
  {
    if (std::abs(v) < 1e-6f)
    {
      return std::numeric_limits<float>::infinity();
    }
    return 1.0f / v;
  };

  const glm::vec3 inv(safeInv(rd.x), safeInv(rd.y), safeInv(rd.z));
  const auto axisTMax = [&](float o, int c, int st, float invComp)
  {
    if (st == 0)
    {
      return std::numeric_limits<float>::infinity();
    }
    return (boundary(o, c, st) - o) * invComp;
  };
  glm::vec3 tMax(axisTMax(origin.x, cell.x, step.x, inv.x),
                 axisTMax(origin.y, cell.y, step.y, inv.y),
                 axisTMax(origin.z, cell.z, step.z, inv.z));
  const glm::vec3 tDelta(std::abs(inv.x), std::abs(inv.y), std::abs(inv.z));
  float t = 0.0f;
  while (t <= maxDist)
  {
    if (visitCell(cell))
    {
      return true;
    }

    if (tMax.x < tMax.y)
    {
      if (tMax.x < tMax.z)
      {
        cell.x += step.x;
        t = tMax.x;
        tMax.x += tDelta.x;
      }
      else
      {
        cell.z += step.z;
        t = tMax.z;
        tMax.z += tDelta.z;
      }
    }
    else
    {
      if (tMax.y < tMax.z)
      {
        cell.y += step.y;
        t = tMax.y;
        tMax.y += tDelta.y;
      }
      else
      {
        cell.z += step.z;
        t = tMax.z;
        tMax.z += tDelta.z;
      }
    }
  }
  return false;
}

} // namespace cutum
