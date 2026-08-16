#pragma once

#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnEmergeState.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

namespace cutum
{

/// Phase 2 SoT: one runtime record per ground column (xz).
/// Dual-written with legacy ColumnEmergeStates until cutover.
struct ColumnRecord
{
  ColumnEmergeState emerge{ColumnEmergeState::Empty};
  ColumnDesiredStage desired{ColumnDesiredStage::None};
  uint32_t content_rev{0};
  uint32_t light_rev{0};
  uint32_t mesh_rev{0};
  /// 0 = idle; else opaque job token (async mesh / relight / gpu).
  uint64_t inflight_job{0};
  bool pending_light{false};
  bool sticky_remesh{false};
  bool light_complete_disk{false};
  bool raa_pending{false};
};

inline uint64_t PackColumnKey(glm::ivec2 xz)
{
  return (static_cast<uint64_t>(static_cast<uint32_t>(xz.x)) << 32) |
         static_cast<uint32_t>(xz.y);
}

/// Thin store mirrored from SetColumnEmergeState / ticket updates.
class UColumnRecordStore
{
public:
  ColumnRecord &GetOrCreate(glm::ivec2 xz)
  {
    return Records[PackColumnKey(xz)];
  }

  const ColumnRecord *Find(glm::ivec2 xz) const
  {
    const auto it = Records.find(PackColumnKey(xz));
    return it == Records.end() ? nullptr : &it->second;
  }

  void SetEmerge(glm::ivec2 xz, ColumnEmergeState state)
  {
    GetOrCreate(xz).emerge = state;
  }

  void SetDesired(glm::ivec2 xz, ColumnDesiredStage stage)
  {
    GetOrCreate(xz).desired = stage;
  }

  void Erase(glm::ivec2 xz) { Records.erase(PackColumnKey(xz)); }

  void Clear() { Records.clear(); }

  size_t Size() const { return Records.size(); }

  template <typename Fn> void ForEach(Fn &&fn) const
  {
    for (const auto &kv : Records)
    {
      const int cx = static_cast<int>(static_cast<uint32_t>(kv.first >> 32));
      const int cz = static_cast<int>(static_cast<uint32_t>(kv.first));
      fn(glm::ivec2(cx, cz), kv.second);
    }
  }

private:
  std::unordered_map<uint64_t, ColumnRecord> Records;
};

} // namespace cutum
