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
  const float base = static_cast<float>(HorizDist(coord, focus));
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

auto MakeDistanceKeyLess(glm::ivec3 focus_ground_chunk, int preferred_cy,
                         bool prefer_lower_cy, bool vertical_valid,
                         const std::function<bool(glm::ivec3)> &missing_mesh,
                         float forward_bias_k, glm::vec2 forward_xz,
                         int focus_radius_for_tail)
{
  return [=](const glm::ivec3 &a, const glm::ivec3 &b)
  {
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
          return ob;
        }
      }
    }
    const float ea = EffectiveHorizDist(a, focus_ground_chunk, forward_bias_k,
                                        forward_xz);
    const float eb = EffectiveHorizDist(b, focus_ground_chunk, forward_bias_k,
                                        forward_xz);
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
  };
}

void SortQueue(std::vector<glm::ivec3> &q, glm::ivec3 focus_ground_chunk,
               int preferred_cy, bool prefer_lower_cy, bool vertical_valid,
               const std::function<bool(glm::ivec3)> &missing_mesh,
               float forward_bias_k, glm::vec2 forward_xz,
               int focus_radius_for_tail)
{
  if (q.size() < 2)
  {
    return;
  }
  std::stable_sort(q.begin(), q.end(),
                   MakeDistanceKeyLess(focus_ground_chunk, preferred_cy,
                                       prefer_lower_cy, vertical_valid,
                                       missing_mesh, forward_bias_k, forward_xz,
                                       focus_radius_for_tail));
}

void PartialSortQueue(std::vector<glm::ivec3> &q, glm::ivec3 focus_ground_chunk,
                      int preferred_cy, bool prefer_lower_cy,
                      bool vertical_valid,
                      const std::function<bool(glm::ivec3)> &missing_mesh,
                      size_t keep_front, float forward_bias_k,
                      glm::vec2 forward_xz, int focus_radius_for_tail)
{
  if (q.size() < 2 || keep_front == 0)
  {
    return;
  }
  if (keep_front >= q.size())
  {
    SortQueue(q, focus_ground_chunk, preferred_cy, prefer_lower_cy,
              vertical_valid, missing_mesh, forward_bias_k, forward_xz,
              focus_radius_for_tail);
    return;
  }
  auto less = MakeDistanceKeyLess(focus_ground_chunk, preferred_cy,
                                  prefer_lower_cy, vertical_valid, missing_mesh,
                                  forward_bias_k, forward_xz,
                                  focus_radius_for_tail);
  std::partial_sort(q.begin(),
                    q.begin() + static_cast<std::ptrdiff_t>(keep_front),
                    q.end(), less);
}

} // namespace

void UChunkDirtySet::EnsureUnified() const
{
  if (!UnifiedDirty)
  {
    return;
  }
  Queue.clear();
  Queue.reserve(FirstMeshQ.size() + RemeshQ.size());
  Queue.insert(Queue.end(), FirstMeshQ.begin(), FirstMeshQ.end());
  Queue.insert(Queue.end(), RemeshQ.begin(), RemeshQ.end());
  UnifiedDirty = false;
}

void UChunkDirtySet::MarkDirty(glm::ivec3 coord)
{
  // Already FirstMesh debt — do not demote.
  if (FirstMeshSet.count(coord) > 0)
  {
    return;
  }
  if (!RemeshSet.insert(coord).second)
  {
    return;
  }
  RemeshQ.push_back(coord);
  InvalidateUnified();
}

void UChunkDirtySet::MarkDirtyPriority(glm::ivec3 coord)
{
  if (RemeshSet.erase(coord) > 0)
  {
    RemeshQ.erase(std::remove(RemeshQ.begin(), RemeshQ.end(), coord),
                  RemeshQ.end());
  }
  if (FirstMeshSet.count(coord) > 0)
  {
    FirstMeshQ.erase(std::remove(FirstMeshQ.begin(), FirstMeshQ.end(), coord),
                     FirstMeshQ.end());
  }
  else
  {
    FirstMeshSet.insert(coord);
  }
  FirstMeshQ.insert(FirstMeshQ.begin(), coord);
  InvalidateUnified();
}

void UChunkDirtySet::Erase(glm::ivec3 coord)
{
  bool erased = false;
  if (FirstMeshSet.erase(coord) > 0)
  {
    FirstMeshQ.erase(std::remove(FirstMeshQ.begin(), FirstMeshQ.end(), coord),
                     FirstMeshQ.end());
    erased = true;
  }
  if (RemeshSet.erase(coord) > 0)
  {
    RemeshQ.erase(std::remove(RemeshQ.begin(), RemeshQ.end(), coord),
                  RemeshQ.end());
    erased = true;
  }
  if (erased)
  {
    InvalidateUnified();
  }
}

void UChunkDirtySet::Clear()
{
  FirstMeshQ.clear();
  RemeshQ.clear();
  FirstMeshSet.clear();
  RemeshSet.clear();
  Queue.clear();
  UnifiedDirty = false;
}

UChunkDirtySet::iterator UChunkDirtySet::RemoveAt(iterator it)
{
  EnsureUnified();
  const glm::ivec3 coord = *it;
  // Erase from owning queue without Invalidate mid-erase of unified.
  if (FirstMeshSet.erase(coord) > 0)
  {
    FirstMeshQ.erase(std::remove(FirstMeshQ.begin(), FirstMeshQ.end(), coord),
                     FirstMeshQ.end());
  }
  if (RemeshSet.erase(coord) > 0)
  {
    RemeshQ.erase(std::remove(RemeshQ.begin(), RemeshQ.end(), coord),
                  RemeshQ.end());
  }
  auto next = Queue.erase(it);
  // Unified still matches except removed element — keep valid.
  return next;
}

void UChunkDirtySet::SortByDistanceKey(
    glm::ivec3 focus_ground_chunk, int preferred_cy, bool prefer_lower_cy,
    bool vertical_valid,
    const std::function<bool(glm::ivec3)> &missing_mesh, float forward_bias_k,
    glm::vec2 forward_xz, int focus_radius_for_tail)
{
  SortQueue(FirstMeshQ, focus_ground_chunk, preferred_cy, prefer_lower_cy,
            vertical_valid, missing_mesh, forward_bias_k, forward_xz,
            focus_radius_for_tail);
  SortQueue(RemeshQ, focus_ground_chunk, preferred_cy, prefer_lower_cy,
            vertical_valid, missing_mesh, forward_bias_k, forward_xz,
            focus_radius_for_tail);
  InvalidateUnified();
}

void UChunkDirtySet::PartialSortByDistanceKey(
    glm::ivec3 focus_ground_chunk, int preferred_cy, bool prefer_lower_cy,
    bool vertical_valid,
    const std::function<bool(glm::ivec3)> &missing_mesh, size_t keep_front,
    float forward_bias_k, glm::vec2 forward_xz, int focus_radius_for_tail)
{
  // Split keep_front across queues: prefer FirstMesh front.
  const size_t fm_front =
      std::min(keep_front, FirstMeshQ.empty() ? size_t{0} : FirstMeshQ.size());
  const size_t rem_front =
      keep_front > fm_front ? keep_front - fm_front : size_t{0};
  PartialSortQueue(FirstMeshQ, focus_ground_chunk, preferred_cy, prefer_lower_cy,
                   vertical_valid, missing_mesh, fm_front, forward_bias_k,
                   forward_xz, focus_radius_for_tail);
  PartialSortQueue(RemeshQ, focus_ground_chunk, preferred_cy, prefer_lower_cy,
                   vertical_valid, missing_mesh, rem_front, forward_bias_k,
                   forward_xz, focus_radius_for_tail);
  InvalidateUnified();
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
  if (!missing_mesh)
  {
    return;
  }
  auto by_missing = [&](const glm::ivec3 &a, const glm::ivec3 &b)
  {
    const bool ma = missing_mesh(a);
    const bool mb = missing_mesh(b);
    return ma != mb ? ma : false;
  };
  if (FirstMeshQ.size() >= 2)
  {
    std::stable_sort(FirstMeshQ.begin(), FirstMeshQ.end(), by_missing);
  }
  if (RemeshQ.size() >= 2)
  {
    std::stable_sort(RemeshQ.begin(), RemeshQ.end(), by_missing);
  }
  InvalidateUnified();
}

int UChunkDirtySet::MaybeDropFarthest(
    glm::ivec3 focus_ground_chunk, size_t soft_cap, int min_keep_horiz,
    const std::function<bool(glm::ivec3)> &missing_mesh)
{
  if (soft_cap == 0 || GetCount() <= soft_cap)
  {
    return 0;
  }
  int dropped = 0;
  while (GetCount() > soft_cap)
  {
    int best_i = -1;
    int best_dist = -1;
    for (size_t i = 0; i < RemeshQ.size(); ++i)
    {
      const glm::ivec3 &c = RemeshQ[i];
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
    const glm::ivec3 victim = RemeshQ[static_cast<size_t>(best_i)];
    Erase(victim);
    ++dropped;
  }
  return dropped;
}

} // namespace cutum
