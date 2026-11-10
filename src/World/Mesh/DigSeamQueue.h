#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <unordered_set>
#include <glm/glm.hpp>

namespace cutum
{

struct DigSeamIVec3Hash
{
  size_t operator()(const glm::ivec3 &v) const noexcept
  {
    const size_t h1 = std::hash<int>()(v.x);
    const size_t h2 = std::hash<int>()(v.y);
    const size_t h3 = std::hash<int>()(v.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

/// FIFO dig face-seam remesh queue (P2 demote → guaranteed drain). Cap 8.
struct DigSeamQueue
{
  static constexpr size_t kCap = 8;

  void Enqueue(glm::ivec3 chunk_coord)
  {
    if (Set.count(chunk_coord) != 0)
    {
      return;
    }
    while (Queue.size() >= kCap)
    {
      const glm::ivec3 oldest = Queue.front();
      Queue.pop_front();
      Set.erase(oldest);
    }
    Queue.push_back(chunk_coord);
    Set.insert(chunk_coord);
  }

  bool Empty() const { return Queue.empty(); }
  size_t Size() const { return Queue.size(); }

  bool TryPop(glm::ivec3 &out)
  {
    if (Queue.empty())
    {
      return false;
    }
    out = Queue.front();
    Queue.pop_front();
    Set.erase(out);
    return true;
  }

  std::deque<glm::ivec3> Queue;
  std::unordered_set<glm::ivec3, DigSeamIVec3Hash> Set;
};

} // namespace cutum
