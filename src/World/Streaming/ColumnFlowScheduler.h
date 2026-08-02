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
  /// True: Admit scans full focus ring (only=nullptr). False: filter to column.
  bool scan_full_focus{false};
  /// >=0 prefer this slice when admitting; -1 = whole remesh band missing slices.
  int cy{-1};
};

/// Single-owner focus column work queue (V4 MVP).
class UColumnFlowScheduler
{
public:
  void Enqueue(glm::ivec2 column, ColumnWorkKind kind, int priority);
  void Enqueue(const ColumnWorkItem &item);
  bool DrainOne(ColumnWorkItem &out);
  void Clear();
  size_t Size() const { return static_cast<size_t>(queue_.size()); }
  bool Contains(glm::ivec2 column, ColumnWorkKind kind) const;
  bool Contains(glm::ivec2 column, ColumnWorkKind kind,
                bool scan_full_focus) const;

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
