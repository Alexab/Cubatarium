#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <queue>
#include <unordered_map>
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
  /// >=0 prefer this slice when admitting.
  /// -1 = whole remesh band missing slices (0..remesh_max).
  /// -2 = whole column missing slices (0..procedural MaxHeight).
  int cy{-1};
};

/// Single-owner focus column work queue (V4). One ticket per column.
/// Higher ColumnWorkKindExclusiveRank may upgrade an occupied column.
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
  /// True if any kind is queued for this column (exclusive mutex).
  bool ContainsColumn(glm::ivec2 column) const;
  uint64_t DeniedCount() const { return denied_n_; }
  void ClearDeniedCount() { denied_n_ = 0; }
  uint64_t UpgradeCount() const { return upgrade_n_; }
  void ClearUpgradeCount() { upgrade_n_ = 0; }

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
  std::unordered_set<int64_t> occupied_columns_;
  /// Live kind for occupied column (for ExclusiveRank upgrade).
  std::unordered_map<int64_t, ColumnWorkKind> occupied_kind_;
  /// Superseded ColumnKey entries still sitting in the priority_queue.
  std::unordered_set<int64_t> cancelled_keys_;
  uint64_t denied_n_{0};
  uint64_t upgrade_n_{0};
};

} // namespace cutum
