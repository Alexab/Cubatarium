#pragma once

#include <glm/glm.hpp>
#include <queue>
#include <unordered_map>
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
  /// Logical queued count (ignores stale bumped duplicates in the heap).
  size_t Size() const { return best_prio_.size(); }
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

  static int64_t ColumnKey(glm::ivec2 column, ColumnWorkKind kind,
                           bool scan_full_focus);

  std::priority_queue<ColumnWorkItem, std::vector<ColumnWorkItem>, Compare>
      queue_;
  /// Best pending priority per key — Enqueue may bump; Drain skips stale.
  std::unordered_map<int64_t, int> best_prio_;
};

} // namespace cutum
