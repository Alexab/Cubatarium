#include "Render/Mesh/ChunkDirtySet.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cutum
{
namespace
{

int HorizDist(glm::ivec3 coord, glm::ivec3 focus)
{
  return std::max(std::abs(coord.x - focus.x), std::abs(coord.z - focus.z));
}

float EffectiveHorizDist(glm::ivec3 coord, glm::ivec3 focus,
                         float forward_bias_k, glm::vec2 forward_xz)
{
  const float base =
      static_cast<float>(HorizDist(coord, focus));
  if (forward_bias_k <= 0.0f)
  {
    return base;
  }
  const float flen =
      std::sqrt(forward_xz.x * forward_xz.x + forward_xz.y * forward_xz.y);
  if (flen < 0.01f)
  {
    return base;
  }
  const float fx = forward_xz.x / flen;
  const float fz = forward_xz.y / flen;
  const float dx = static_cast<float>(coord.x - focus.x);
  const float dz = static_cast<float>(coord.z - focus.z);
  const float clen = std::sqrt(dx * dx + dz * dz);
  if (clen < 0.01f)
  {
    return base;
  }
  const float bias = std::max(0.0f, (dx / clen) * fx + (dz / clen) * fz);
  return base - forward_bias_k * bias;
}

} // namespace

void UChunkDirtySet::MarkDirty(glm::ivec3 coord)
{
  if (!Set.insert(coord).second)
  {
    return;
  }
  Queue.push_back(coord);
}

void UChunkDirtySet::MarkDirtyPriority(glm::ivec3 coord)
{
  if (Set.count(coord))
  {
    Queue.erase(std::remove(Queue.begin(), Queue.end(), coord), Queue.end());
  }
  else
  {
    Set.insert(coord);
  }
  Queue.insert(Queue.begin(), coord);
}

void UChunkDirtySet::Erase(glm::ivec3 coord)
{
  if (!Set.erase(coord))
  {
    return;
  }
  Queue.erase(std::remove(Queue.begin(), Queue.end(), coord), Queue.end());
}

void UChunkDirtySet::Clear()
{
  Queue.clear();
  Set.clear();
}

UChunkDirtySet::iterator UChunkDirtySet::RemoveAt(iterator it)
{
  Set.erase(*it);
  return Queue.erase(it);
}

void UChunkDirtySet::SortByDistanceKey(
    glm::ivec3 focus_ground_chunk, int preferred_cy, bool prefer_lower_cy,
    bool vertical_valid,
    const std::function<bool(glm::ivec3)> &missing_mesh, float forward_bias_k,
    glm::vec2 forward_xz, int focus_radius_for_tail)
{
  if (Queue.size() < 2)
  {
    return;
  }
  std::stable_sort(
      Queue.begin(), Queue.end(),
      [&](const glm::ivec3 &a, const glm::ivec3 &b)
      {
        // Class: missing mesh before remesh (even if farther in focus bubble).
        if (missing_mesh)
        {
          const bool ma = missing_mesh(a);
          const bool mb = missing_mesh(b);
          if (ma != mb)
          {
            return ma;
          }
          if (!ma && !mb && focus_radius_for_tail >= 0)
          {
            const int ha = HorizDist(a, focus_ground_chunk);
            const int hb = HorizDist(b, focus_ground_chunk);
            const bool oa = ha > focus_radius_for_tail;
            const bool ob = hb > focus_radius_for_tail;
            if (oa != ob)
            {
              return ob; // in-focus remesh before outside-focus remesh
            }
          }
        }
        const float ea = EffectiveHorizDist(a, focus_ground_chunk,
                                           forward_bias_k, forward_xz);
        const float eb = EffectiveHorizDist(b, focus_ground_chunk,
                                           forward_bias_k, forward_xz);
        if (ea != eb)
        {
          return ea < eb;
        }
        if (vertical_valid)
        {
          if (prefer_lower_cy)
          {
            if (a.y != b.y)
            {
              return a.y < b.y;
            }
          }
          else
          {
            const int da = std::abs(a.y - preferred_cy);
            const int db = std::abs(b.y - preferred_cy);
            if (da != db)
            {
              return da < db;
            }
          }
        }
        return false;
      });
}

void UChunkDirtySet::PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk,
                                              int radius_chunks)
{
  (void)radius_chunks;
  SortByDistanceKey(focus_ground_chunk, 0, false, false, {});
}

void UChunkDirtySet::PrioritizeVerticalCy(glm::ivec3 focus_ground_chunk,
                                          int radius_chunks, int preferred_cy,
                                          bool prefer_lower_cy)
{
  (void)radius_chunks;
  SortByDistanceKey(focus_ground_chunk, preferred_cy, prefer_lower_cy, true,
                    {});
}

void UChunkDirtySet::PrioritizeChunksWithoutMesh(
    const std::function<bool(glm::ivec3)> &missing_mesh)
{
  if (Queue.size() < 2 || !missing_mesh)
  {
    return;
  }
  std::stable_sort(Queue.begin(), Queue.end(),
                   [&](const glm::ivec3 &a, const glm::ivec3 &b)
                   {
                     const bool ma = missing_mesh(a);
                     const bool mb = missing_mesh(b);
                     if (ma != mb)
                     {
                       return ma;
                     }
                     return false;
                   });
}

int UChunkDirtySet::MaybeDropFarthest(
    glm::ivec3 focus_ground_chunk, size_t soft_cap, int min_keep_horiz,
    const std::function<bool(glm::ivec3)> &missing_mesh)
{
  if (soft_cap == 0 || Queue.size() <= soft_cap)
  {
    return 0;
  }
  int dropped = 0;
  while (Queue.size() > soft_cap)
  {
    int best_i = -1;
    int best_dist = -1;
    for (size_t i = 0; i < Queue.size(); ++i)
    {
      const glm::ivec3 &c = Queue[i];
      const int d = HorizDist(c, focus_ground_chunk);
      if (d <= min_keep_horiz)
      {
        continue;
      }
      if (missing_mesh && missing_mesh(c))
      {
        continue;
      }
      if (d > best_dist)
      {
        best_dist = d;
        best_i = static_cast<int>(i);
      }
    }
    if (best_i < 0)
    {
      break;
    }
    const glm::ivec3 victim = Queue[static_cast<size_t>(best_i)];
    Erase(victim);
    ++dropped;
  }
  return dropped;
}

} // namespace cutum
