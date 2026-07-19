#include "Render/Mesh/ChunkDirtySet.h"

#include <algorithm>
#include <utility>

namespace cutum
{
namespace
{

int HorizDist(glm::ivec3 coord, glm::ivec3 focus)
{
  return std::max(std::abs(coord.x - focus.x), std::abs(coord.z - focus.z));
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
    const std::function<bool(glm::ivec3)> &missing_mesh)
{
  if (Queue.size() < 2)
  {
    return;
  }
  std::stable_sort(
      Queue.begin(), Queue.end(),
      [&](const glm::ivec3 &a, const glm::ivec3 &b)
      {
        const int ha = HorizDist(a, focus_ground_chunk);
        const int hb = HorizDist(b, focus_ground_chunk);
        if (ha != hb)
        {
          return ha < hb;
        }
        if (missing_mesh)
        {
          const bool ma = missing_mesh(a);
          const bool mb = missing_mesh(b);
          if (ma != mb)
          {
            return ma;
          }
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

} // namespace cutum
