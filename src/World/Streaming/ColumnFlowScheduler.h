#pragma once

#include <cstdint>
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
  /// >=0 prefer this slice when admitting.
  /// -1 = whole remesh band missing slices (0..remesh_max).
  /// -2 = whole column missing slices (0..procedural MaxHeight).
  int cy{-1};
  /// Monotonic ticket generation for this column occupancy (M02 / A05).
  uint64_t generation{0};
};

/// Full-width column coordinate (no 16-bit truncation).
struct ColumnCoord
{
  int x{0};
  int z{0};

  ColumnCoord() = default;
  explicit ColumnCoord(glm::ivec2 c) : x(c.x), z(c.y) {}
  ColumnCoord(int x_, int z_) : x(x_), z(z_) {}

  glm::ivec2 IVec() const { return glm::ivec2(x, z); }

  bool operator==(const ColumnCoord &o) const
  {
    return x == o.x && z == o.z;
  }
};

struct ColumnCoordHash
{
  size_t operator()(const ColumnCoord &c) const noexcept
  {
    const uint64_t ux = static_cast<uint32_t>(c.x);
    const uint64_t uz = static_cast<uint32_t>(c.z);
    return static_cast<size_t>((ux << 32) ^ uz);
  }
};

/// Single-owner focus column work queue (V4 / M02).
/// Live map is authoritative; heap may contain stale generations (lazy delete).
class UColumnFlowScheduler
{
public:
  void Enqueue(glm::ivec2 column, ColumnWorkKind kind, int priority);
  void Enqueue(const ColumnWorkItem &item);
  bool DrainOne(ColumnWorkItem &out);
  void Clear();

  /// Live tickets only (excludes superseded heap entries).
  size_t LiveCount() const { return live_.size(); }
  size_t HeapCount() const { return static_cast<size_t>(heap_.size()); }
  size_t StaleCount() const
  {
    return HeapCount() >= LiveCount() ? HeapCount() - LiveCount() : 0;
  }
  /// Alias for LiveCount — callers expecting "queue size" mean live work.
  size_t Size() const { return LiveCount(); }

  bool Contains(glm::ivec2 column, ColumnWorkKind kind) const;
  bool Contains(glm::ivec2 column, ColumnWorkKind kind,
                bool scan_full_focus) const;
  /// True if any kind is queued for this column (exclusive mutex).
  bool ContainsColumn(glm::ivec2 column) const;

  template <typename Fn>
  void ForEachOccupiedColumn(Fn &&fn) const
  {
    for (const auto &kv : live_)
    {
      fn(kv.first.IVec());
    }
  }

  uint64_t DeniedCount() const { return denied_n_; }
  void ClearDeniedCount() { denied_n_ = 0; }
  uint64_t UpgradeCount() const { return upgrade_n_; }
  void ClearUpgradeCount() { upgrade_n_ = 0; }
  uint64_t SupersededCount() const { return superseded_n_; }
  void ClearSupersededCount() { superseded_n_ = 0; }

private:
  struct LiveTicket
  {
    ColumnWorkKind kind{ColumnWorkKind::FirstMesh};
    int priority{0};
    bool scan_full_focus{false};
    int cy{-1};
    uint64_t generation{0};
  };

  struct HeapEntry
  {
    ColumnWorkItem item{};
    uint64_t sequence{0};
  };

  struct Compare
  {
    bool operator()(const HeapEntry &a, const HeapEntry &b) const
    {
      if (a.item.priority != b.item.priority)
      {
        return a.item.priority < b.item.priority;
      }
      // Stable tie-break: older sequence first when priorities equal.
      return a.sequence > b.sequence;
    }
  };

  void PushLive(const ColumnWorkItem &item);

  std::priority_queue<HeapEntry, std::vector<HeapEntry>, Compare> heap_;
  std::unordered_map<ColumnCoord, LiveTicket, ColumnCoordHash> live_;
  uint64_t next_generation_{1};
  uint64_t next_sequence_{1};
  uint64_t denied_n_{0};
  uint64_t upgrade_n_{0};
  uint64_t superseded_n_{0};
};

} // namespace cutum
