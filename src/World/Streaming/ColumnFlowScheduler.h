#pragma once

#include <glm/glm.hpp>
#include <queue>
#include <unordered_set>
#include <vector>

namespace cutum
{

enum class ColumnWorkKind : uint8_t
{
  RelightThenMesh = 0,
  FirstMesh = 1,
  RemeshSeam = 2,
  PromoteRelight = 3,
};

struct ColumnWorkItem
{
  glm::ivec2 column{0};
  ColumnWorkKind kind{ColumnWorkKind::FirstMesh};
  int priority{0};
};

/// Single-owner focus column work queue (V4 MVP).
class UColumnFlowScheduler
{
public:
  void Enqueue(glm::ivec2 column, ColumnWorkKind kind, int priority);
  bool DrainOne(ColumnWorkItem &out);
  void Clear();
  size_t Size() const { return static_cast<size_t>(queue_.size()); }

private:
  struct Compare
  {
    bool operator()(const ColumnWorkItem &a, const ColumnWorkItem &b) const
    {
      return a.priority < b.priority;
    }
  };

  std::priority_queue<ColumnWorkItem, std::vector<ColumnWorkItem>, Compare>
      queue_;
  std::unordered_set<int64_t> inflight_;
};

} // namespace cutum
