#include "Render/Mesh/ChunkDirtySet.h"

#include "World/Streaming/RelightFifoPolicy.h"

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

template <typename RandomIt>
void SortQueueRange(RandomIt begin, RandomIt end,
                    glm::ivec3 focus_ground_chunk, int preferred_cy,
                    bool prefer_lower_cy, bool vertical_valid,
                    const std::function<bool(glm::ivec3)> &missing_mesh,
                    float forward_bias_k, glm::vec2 forward_xz,
                    int focus_radius_for_tail)
{
  if (end - begin < 2)
  {
    return;
  }
  std::stable_sort(begin, end,
                   MakeDistanceKeyLess(focus_ground_chunk, preferred_cy,
                                       prefer_lower_cy, vertical_valid,
                                       missing_mesh, forward_bias_k, forward_xz,
                                       focus_radius_for_tail));
}

void SortQueue(std::vector<glm::ivec3> &q, glm::ivec3 focus_ground_chunk,
               int preferred_cy, bool prefer_lower_cy, bool vertical_valid,
               const std::function<bool(glm::ivec3)> &missing_mesh,
               float forward_bias_k, glm::vec2 forward_xz,
               int focus_radius_for_tail)
{
  SortQueueRange(q.begin(), q.end(), focus_ground_chunk, preferred_cy,
                 prefer_lower_cy, vertical_valid, missing_mesh,
                 forward_bias_k, forward_xz, focus_radius_for_tail);
}

template <typename RandomIt>
void PartialSortQueueRange(RandomIt begin, RandomIt end,
                           glm::ivec3 focus_ground_chunk, int preferred_cy,
                           bool prefer_lower_cy, bool vertical_valid,
                           const std::function<bool(glm::ivec3)> &missing_mesh,
                           size_t keep_front, float forward_bias_k,
                           glm::vec2 forward_xz, int focus_radius_for_tail)
{
  const size_t count = static_cast<size_t>(end - begin);
  if (count < 2 || keep_front == 0)
  {
    return;
  }
  auto less = MakeDistanceKeyLess(focus_ground_chunk, preferred_cy,
                                  prefer_lower_cy, vertical_valid, missing_mesh,
                                  forward_bias_k, forward_xz,
                                  focus_radius_for_tail);
  if (keep_front >= count)
  {
    std::stable_sort(begin, end, less);
    return;
  }
  std::partial_sort(begin,
                    begin + static_cast<std::ptrdiff_t>(keep_front), end, less);
}

void PartialSortQueue(std::vector<glm::ivec3> &q, glm::ivec3 focus_ground_chunk,
                      int preferred_cy, bool prefer_lower_cy,
                      bool vertical_valid,
                      const std::function<bool(glm::ivec3)> &missing_mesh,
                      size_t keep_front, float forward_bias_k,
                      glm::vec2 forward_xz, int focus_radius_for_tail)
{
  PartialSortQueueRange(q.begin(), q.end(), focus_ground_chunk, preferred_cy,
                        prefer_lower_cy, vertical_valid, missing_mesh,
                        keep_front, forward_bias_k, forward_xz,
                        focus_radius_for_tail);
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

void UChunkDirtySet::NoteColumnAdd(glm::ivec3 coord)
{
  const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(coord.x))
                        << 32) |
                       static_cast<uint32_t>(coord.z);
  ColumnCounts[key] += 1;
}

void UChunkDirtySet::NoteColumnRemove(glm::ivec3 coord)
{
  const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(coord.x))
                        << 32) |
                       static_cast<uint32_t>(coord.z);
  const auto it = ColumnCounts.find(key);
  if (it == ColumnCounts.end())
  {
    return;
  }
  if (--it->second <= 0)
  {
    ColumnCounts.erase(it);
  }
}

int UChunkDirtySet::CountWithinHorizontalRadius(glm::ivec3 center_chunk,
                                                int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return 0;
  }
  int count = 0;
  for (int dx = -radius_chunks; dx <= radius_chunks; ++dx)
  {
    for (int dz = -radius_chunks; dz <= radius_chunks; ++dz)
    {
      if (std::max(std::abs(dx), std::abs(dz)) > radius_chunks)
      {
        continue;
      }
      const int x = center_chunk.x + dx;
      const int z = center_chunk.z + dz;
      const uint64_t key =
          (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
          static_cast<uint32_t>(z);
      const auto it = ColumnCounts.find(key);
      if (it != ColumnCounts.end())
      {
        count += it->second;
      }
    }
  }
  return count;
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
  const bool screen_ray_priority =
      DeferredScreenRayRemeshSet.erase(coord) > 0;
  const bool priority =
      DeferredPriorityRemeshSet.erase(coord) > 0 || screen_ray_priority;
  if (priority)
  {
    PriorityRemeshSet.insert(coord);
    if (screen_ray_priority)
    {
      ScreenRayRemeshSet.insert(coord);
      const auto screen_ray_end = std::find_if(
          RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 queued)
          { return ScreenRayRemeshSet.count(queued) == 0; });
      RemeshQ.insert(screen_ray_end, coord);
    }
    else
    {
      const auto priority_end = std::find_if(
          RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 queued)
          { return PriorityRemeshSet.count(queued) == 0; });
      RemeshQ.insert(priority_end, coord);
    }
  }
  else
  {
    RemeshQ.push_back(coord);
  }
  EnqueueFrameByCoord.emplace(coord, ScheduleFrame);
  NoteColumnAdd(coord);
  InvalidateUnified();
}

void UChunkDirtySet::MarkDirtyPriority(glm::ivec3 coord)
{
  DeferredPriorityRemeshSet.erase(coord);
  DeferredScreenRayRemeshSet.erase(coord);
  const bool was_remesh = RemeshSet.erase(coord) > 0;
  PriorityRemeshSet.erase(coord);
  ScreenRayRemeshSet.erase(coord);
  if (was_remesh)
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
    if (!was_remesh)
    {
      NoteColumnAdd(coord);
    }
  }
  EnqueueFrameByCoord.emplace(coord, ScheduleFrame);
  FirstMeshQ.insert(FirstMeshQ.begin(), coord);
  InvalidateUnified();
}

bool UChunkDirtySet::PrioritizeRemesh(glm::ivec3 coord)
{
  if (FirstMeshSet.count(coord) > 0)
  {
    DeferredPriorityRemeshSet.erase(coord);
    DeferredScreenRayRemeshSet.erase(coord);
    return false;
  }
  if (RemeshSet.count(coord) == 0)
  {
    // A running build can own the coord while its follow-up is parked in
    // RemeshAfterApply. Keep the priority request until that ticket is enqueued.
    DeferredPriorityRemeshSet.insert(coord);
    return true;
  }
  if (PriorityRemeshSet.count(coord) > 0)
  {
    // Repeated visible demand must not rotate an existing priority item ahead
    // of older repairs or reset the lane's stable order.
    return true;
  }
  PriorityRemeshSet.insert(coord);
  const auto it = std::find(RemeshQ.begin(), RemeshQ.end(), coord);
  if (it != RemeshQ.end())
  {
    RemeshQ.erase(it);
    const auto priority_end = std::find_if(
        RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 queued)
        { return PriorityRemeshSet.count(queued) == 0; });
    RemeshQ.insert(priority_end, coord);
  }
  InvalidateUnified();
  return true;
}

bool UChunkDirtySet::PrioritizeScreenRayRemesh(glm::ivec3 coord)
{
  if (FirstMeshSet.count(coord) > 0)
  {
    DeferredPriorityRemeshSet.erase(coord);
    DeferredScreenRayRemeshSet.erase(coord);
    return false;
  }
  if (RemeshSet.count(coord) == 0)
  {
    // The active build/RAA owns the follow-up. Transfer the ray urgency with
    // that concrete deferred ticket when MarkDirty enqueues it.
    DeferredPriorityRemeshSet.insert(coord);
    DeferredScreenRayRemeshSet.insert(coord);
    return true;
  }
  if (ScreenRayRemeshSet.count(coord) > 0)
  {
    // Keep one successor pin while this ticket is owned by the queue. If the
    // queue item is consumed before a later face-debt invalidation, that next
    // Dirty ticket must inherit the exact screen-ray witness.
    DeferredPriorityRemeshSet.insert(coord);
    DeferredScreenRayRemeshSet.insert(coord);
    return true;
  }
  PriorityRemeshSet.insert(coord);
  ScreenRayRemeshSet.insert(coord);
  DeferredPriorityRemeshSet.insert(coord);
  DeferredScreenRayRemeshSet.insert(coord);
  const auto it = std::find(RemeshQ.begin(), RemeshQ.end(), coord);
  if (it != RemeshQ.end())
  {
    RemeshQ.erase(it);
    const auto screen_ray_end = std::find_if(
        RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 queued)
        { return ScreenRayRemeshSet.count(queued) == 0; });
    RemeshQ.insert(screen_ray_end, coord);
    InvalidateUnified();
  }
  return true;
}

void UChunkDirtySet::ClearDeferredScreenRayRemesh(glm::ivec3 coord)
{
  if (DeferredScreenRayRemeshSet.erase(coord) > 0)
  {
    DeferredPriorityRemeshSet.erase(coord);
  }
}

void UChunkDirtySet::Erase(glm::ivec3 coord)
{
  DeferredPriorityRemeshSet.erase(coord);
  DeferredScreenRayRemeshSet.erase(coord);
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
  PriorityRemeshSet.erase(coord);
  ScreenRayRemeshSet.erase(coord);
  if (erased)
  {
    EnqueueFrameByCoord.erase(coord);
    NoteColumnRemove(coord);
    InvalidateUnified();
  }
}

void UChunkDirtySet::Clear()
{
  FirstMeshQ.clear();
  RemeshQ.clear();
  FirstMeshSet.clear();
  RemeshSet.clear();
  PriorityRemeshSet.clear();
  ScreenRayRemeshSet.clear();
  DeferredPriorityRemeshSet.clear();
  DeferredScreenRayRemeshSet.clear();
  EnqueueFrameByCoord.clear();
  Queue.clear();
  ColumnCounts.clear();
  UnifiedDirty = false;
}

UChunkDirtySet::iterator UChunkDirtySet::RemoveAt(iterator it)
{
  EnsureUnified();
  const glm::ivec3 coord = *it;
  const bool preserve_screen_ray_successor =
      ScreenRayRemeshSet.count(coord) > 0;
  if (preserve_screen_ray_successor)
  {
    DeferredPriorityRemeshSet.insert(coord);
    DeferredScreenRayRemeshSet.insert(coord);
  }
  else
  {
    DeferredPriorityRemeshSet.erase(coord);
    DeferredScreenRayRemeshSet.erase(coord);
  }
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
  PriorityRemeshSet.erase(coord);
  ScreenRayRemeshSet.erase(coord);
  EnqueueFrameByCoord.erase(coord);
  NoteColumnRemove(coord);
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
  const auto screen_ray_end = std::stable_partition(
      RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 coord)
      { return ScreenRayRemeshSet.count(coord) > 0; });
  const auto priority_end = std::stable_partition(
      screen_ray_end, RemeshQ.end(), [&](glm::ivec3 coord)
      { return PriorityRemeshSet.count(coord) > 0; });
  // Keep exact pixel-hit repairs in a stable head lane. Distance sorting the
  // entire RemeshQ used to move a just-promoted screen witness behind dozens
  // of unrelated priority entries before the renderer sampled that frame.
  SortQueueRange(priority_end, RemeshQ.end(), focus_ground_chunk,
                 preferred_cy, prefer_lower_cy, vertical_valid, missing_mesh,
                 forward_bias_k, forward_xz, focus_radius_for_tail);
  InvalidateUnified();
}

void UChunkDirtySet::BoostJustRelitNear(glm::ivec3 focus_ground_chunk,
                                        glm::ivec2 relit_xz, int max_horiz)
{
  if (FirstMeshQ.size() < 2)
  {
    return;
  }
  auto boosted = [&](const glm::ivec3 &c)
  {
    const int h = HorizDist(c, focus_ground_chunk);
    const bool just = c.x == relit_xz.x && c.z == relit_xz.y;
    return ShouldFirstMeshSortBoost(h, just, max_horiz);
  };
  std::stable_partition(FirstMeshQ.begin(), FirstMeshQ.end(), boosted);
  InvalidateUnified();
}

void UChunkDirtySet::BoostForwardApproachFirstMesh(
    glm::ivec3 focus_ground_chunk, glm::vec2 forward_xz, int near_horiz,
    int max_approach_horiz, int max_vertical_delta,
    size_t distance_sorted_prefix, int preferred_cy, bool prefer_lower_cy,
    bool vertical_valid, float forward_bias_k)
{
  if (FirstMeshQ.size() < 2 || max_approach_horiz <= near_horiz)
  {
    return;
  }
  const float forward_len =
      std::sqrt(forward_xz.x * forward_xz.x + forward_xz.y * forward_xz.y);
  if (forward_len < 0.01f)
  {
    return;
  }
  const float forward_x = forward_xz.x / forward_len;
  const float forward_z = forward_xz.y / forward_len;
  const int near_limit = std::max(0, near_horiz);
  const int approach_limit = std::max(near_limit, max_approach_horiz);
  const int vertical_limit = std::max(0, max_vertical_delta);
  const auto forward_dot = [&](const glm::ivec3 &coord)
  {
    const float dx = static_cast<float>(coord.x - focus_ground_chunk.x);
    const float dz = static_cast<float>(coord.z - focus_ground_chunk.z);
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance < 0.01f)
    {
      return 1.0f;
    }
    return (dx * forward_x + dz * forward_z) / distance;
  };
  const auto priority_band = [&](const glm::ivec3 &coord)
  {
    const int horiz = HorizDist(coord, focus_ground_chunk);
    const int vertical = std::abs(coord.y - focus_ground_chunk.y);
    if (horiz <= near_limit && vertical <= vertical_limit)
    {
      return 0; // near-FOV work in the camera's active vertical band
    }
    if (horiz > near_limit && horiz <= approach_limit &&
        vertical <= vertical_limit && forward_dot(coord) >= 0.5f)
    {
      return 1; // visible approach work just outside the near ring
    }
    if (horiz <= near_limit)
    {
      return 2; // nearby but vertically outside the camera band
    }
    return 3;
  };
  const auto in_approach_sector = [&](const glm::ivec3 &coord)
  { return priority_band(coord) < 3; };
  const auto always_missing = [](glm::ivec3) { return true; };
  const auto distance_less = MakeDistanceKeyLess(
      focus_ground_chunk, preferred_cy, prefer_lower_cy, vertical_valid,
      std::function<bool(glm::ivec3)>(always_missing), forward_bias_k,
      forward_xz, -1);
  const auto sorted_suffix_less = [&](const glm::ivec3 &a,
                                      const glm::ivec3 &b)
  {
    const int band_a = priority_band(a);
    const int band_b = priority_band(b);
    if (band_a != band_b)
    {
      return band_a < band_b;
    }
    if (distance_less(a, b))
    {
      return true;
    }
    if (distance_less(b, a))
    {
      return false;
    }
    const uint64_t age_a = GetEnqueueAgeFrames(a);
    const uint64_t age_b = GetEnqueueAgeFrames(b);
    if (age_a != age_b)
    {
      return age_a > age_b;
    }
    if (a.x != b.x)
    {
      return a.x < b.x;
    }
    if (a.y != b.y)
    {
      return a.y < b.y;
    }
    return a.z < b.z;
  };

  // Keep the camera-distance prefix intact: PartialSortByDistanceKey already
  // ranked it with the forward bias and preferred vertical band. Only promote
  // and reorder approach candidates from the unspecified suffix.
  const size_t prefix_count =
      std::min(distance_sorted_prefix, FirstMeshQ.size());
  if (prefix_count == FirstMeshQ.size())
  {
    std::stable_partition(FirstMeshQ.begin(), FirstMeshQ.end(),
                          in_approach_sector);
  }
  else
  {
    const auto prefix_end =
        FirstMeshQ.begin() + static_cast<std::ptrdiff_t>(prefix_count);
    const auto prefix_approach_end = std::stable_partition(
        FirstMeshQ.begin(), prefix_end, in_approach_sector);
    const auto suffix_approach_end = std::stable_partition(
        prefix_end, FirstMeshQ.end(), in_approach_sector);
    std::stable_sort(prefix_end, suffix_approach_end, sorted_suffix_less);
    std::rotate(prefix_approach_end, prefix_end, suffix_approach_end);
  }
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
  const auto screen_ray_end = std::stable_partition(
      RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 coord)
      { return ScreenRayRemeshSet.count(coord) > 0; });
  const auto priority_end = std::stable_partition(
      screen_ray_end, RemeshQ.end(), [&](glm::ivec3 coord)
      { return PriorityRemeshSet.count(coord) > 0; });
  const size_t priority_count =
      static_cast<size_t>(priority_end - RemeshQ.begin());
  const size_t ordinary_keep_front =
      rem_front > priority_count ? rem_front - priority_count : 0;
  PartialSortQueueRange(priority_end, RemeshQ.end(), focus_ground_chunk,
                        preferred_cy, prefer_lower_cy, vertical_valid,
                        missing_mesh, ordinary_keep_front, forward_bias_k,
                        forward_xz, focus_radius_for_tail);
  InvalidateUnified();
}

void UChunkDirtySet::PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk,
                                              int radius_chunks)
{
  (void)radius_chunks;
  SortByDistanceKey(focus_ground_chunk, 0, false, false, {});
}

void UChunkDirtySet::PrioritizeAgedNearHorizontal(
    glm::ivec3 focus_ground_chunk, int radius_chunks,
    uint64_t minimum_age_frames)
{
  const int radius = std::max(0, radius_chunks);
  const auto is_overdue_in_focus = [&](glm::ivec3 coord)
  {
    const auto it = EnqueueFrameByCoord.find(coord);
    if (it == EnqueueFrameByCoord.end())
    {
      return false;
    }
    const uint64_t age = ScheduleFrame >= it->second
                             ? ScheduleFrame - it->second
                             : 0;
    return age >= minimum_age_frames &&
           HorizDist(coord, focus_ground_chunk) <= radius;
  };
  const auto enqueue_frame = [&](glm::ivec3 coord)
  {
    const auto it = EnqueueFrameByCoord.find(coord);
    return it == EnqueueFrameByCoord.end() ? ScheduleFrame : it->second;
  };
  auto less = [&](const glm::ivec3 &a, const glm::ivec3 &b)
  {
    const bool a_overdue = is_overdue_in_focus(a);
    const bool b_overdue = is_overdue_in_focus(b);
    if (a_overdue != b_overdue)
    {
      return a_overdue;
    }
    if (!a_overdue)
    {
      // Preserve the queue's existing camera-forward and preferred-Y order.
      // It was established by SortByDistanceKey; sorting these entries again
      // by horizontal distance alone erases both priorities under hole load.
      return false;
    }
    // Age takes precedence across the bounded focus region so continuous near
    // arrivals cannot starve older FirstMesh work farther along the route.
    return enqueue_frame(a) < enqueue_frame(b);
  };
  if (FirstMeshQ.size() > 1)
  {
    std::stable_sort(FirstMeshQ.begin(), FirstMeshQ.end(), less);
  }
  InvalidateUnified();
}

void UChunkDirtySet::PrioritizeAgedPriorityRemeshNearHorizontal(
    glm::ivec3 focus_ground_chunk, int radius_chunks,
    uint64_t minimum_age_frames)
{
  if (RemeshQ.size() < 2)
  {
    return;
  }

  // The screen-ray sub-prefix is a direct pixel witness; age fairness applies
  // to the remaining priority lane without reordering those exact hits.
  const auto screen_ray_end = std::find_if(
      RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 coord)
      { return ScreenRayRemeshSet.count(coord) == 0; });
  const auto priority_end = std::find_if(
      screen_ray_end, RemeshQ.end(), [&](glm::ivec3 coord)
      { return PriorityRemeshSet.count(coord) == 0; });
  if (priority_end - screen_ray_end < 2)
  {
    return;
  }

  const int radius = std::max(0, radius_chunks);
  const auto is_overdue_in_focus = [&](glm::ivec3 coord)
  {
    return GetEnqueueAgeFrames(coord) >= minimum_age_frames &&
           HorizDist(coord, focus_ground_chunk) <= radius;
  };
  const auto older_first = [&](const glm::ivec3 &a, const glm::ivec3 &b)
  {
    const bool a_overdue = is_overdue_in_focus(a);
    const bool b_overdue = is_overdue_in_focus(b);
    if (a_overdue != b_overdue)
    {
      return a_overdue;
    }
    if (!a_overdue)
    {
      // Preserve the existing camera-distance order among other entries.
      return false;
    }
    return GetEnqueueAgeFrames(a) > GetEnqueueAgeFrames(b);
  };
  bool saw_non_overdue = false;
  bool priority_order_is_stale = false;
  uint64_t previous_overdue_age = UINT64_MAX;
  for (auto it = screen_ray_end; it != priority_end; ++it)
  {
    const glm::ivec3 coord = *it;
    if (!is_overdue_in_focus(coord))
    {
      saw_non_overdue = true;
      continue;
    }
    const uint64_t age = GetEnqueueAgeFrames(coord);
    if (saw_non_overdue || age > previous_overdue_age)
    {
      priority_order_is_stale = true;
      break;
    }
    previous_overdue_age = age;
  }
  if (!priority_order_is_stale)
  {
    return;
  }
  std::stable_sort(screen_ray_end, priority_end, older_first);
  InvalidateUnified();
}

uint64_t UChunkDirtySet::GetEnqueueAgeFrames(glm::ivec3 coord) const
{
  const auto it = EnqueueFrameByCoord.find(coord);
  if (it == EnqueueFrameByCoord.end() || ScheduleFrame < it->second)
  {
    return 0;
  }
  return ScheduleFrame - it->second;
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
    const auto screen_ray_end = std::stable_partition(
        RemeshQ.begin(), RemeshQ.end(), [&](glm::ivec3 coord)
        { return ScreenRayRemeshSet.count(coord) > 0; });
    const auto priority_end = std::stable_partition(
        screen_ray_end, RemeshQ.end(), [&](glm::ivec3 coord)
        { return PriorityRemeshSet.count(coord) > 0; });
    std::stable_sort(priority_end, RemeshQ.end(), by_missing);
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
  constexpr int kMaxDropPerCall = 4; // A28 T1
  while (GetCount() > soft_cap && dropped < kMaxDropPerCall)
  {
    int best_i = -1;
    int best_dist = -1;
    for (size_t i = 0; i < RemeshQ.size(); ++i)
    {
      const glm::ivec3 &c = RemeshQ[i];
      // A priority remesh is an exact visible-light repair admitted after the
      // renderer rejected this slice. Dropping it here would leave its
      // bounded retry ticket alive without a queue owner, creating long gaps
      // before it can be admitted again. Soft-cap only ordinary remesh work.
      if (PriorityRemeshSet.count(c) > 0)
      {
        continue;
      }
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
