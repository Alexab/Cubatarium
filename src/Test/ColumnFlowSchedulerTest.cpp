#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRenderablePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::ColumnWorkItem;
  using cutum::ColumnWorkKind;
  using cutum::UColumnFlowScheduler;

  UColumnFlowScheduler s;
  s.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 10);
  s.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 99); // refresh urgency
  Expect(s.Size() == 1, "same column+kind refreshes to one live ticket");
  s.Enqueue({1, 2}, ColumnWorkKind::RelightThenMesh, 5);
  Expect(s.Size() == 1, "lower-rank RelightThenMesh on FirstMesh denied");
  Expect(s.ContainsColumn({1, 2}), "column occupied");
  Expect(s.DeniedCount() >= 1, "denied competing lower producer");
  // ColPipe P1: higher ExclusiveRank upgrades occupied column.
  {
    UColumnFlowScheduler u;
    u.Enqueue({9, 9}, ColumnWorkKind::RemeshSeam, 30);
    u.Enqueue({9, 9}, ColumnWorkKind::FirstMesh, 80);
    Expect(u.UpgradeCount() >= 1, "RemeshSeam→FirstMesh upgrades");
    Expect(u.Contains({9, 9}, ColumnWorkKind::FirstMesh),
           "upgraded to FirstMesh");
    Expect(!u.Contains({9, 9}, ColumnWorkKind::RemeshSeam),
           "old RemeshSeam cancelled");
    ColumnWorkItem up{};
    Expect(u.DrainOne(up), "drain upgraded");
    Expect(up.kind == ColumnWorkKind::FirstMesh, "drained FirstMesh");
    Expect(!u.DrainOne(up), "no stale RemeshSeam after upgrade");
  }
  ColumnWorkItem a{};
  Expect(s.DrainOne(a), "drain first");
  Expect(a.priority == 99, "refreshed urgency kept");
  Expect(a.kind == ColumnWorkKind::FirstMesh, "kind FirstMesh");
  Expect(!s.ContainsColumn({1, 2}), "column free after drain");
  Expect(!s.DrainOne(a), "empty");

  // TD-ARCH-026: RemeshSeam is the hide=>repair ticket kind.
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 30);
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 99);
  Expect(s.Size() == 1, "RemeshSeam ticket refreshed to one live");
  Expect(s.Contains({3, 4}, ColumnWorkKind::RemeshSeam),
         "Contains RemeshSeam while queued");
  ColumnWorkItem c{};
  Expect(s.DrainOne(c), "drain RemeshSeam");
  Expect(c.kind == ColumnWorkKind::RemeshSeam, "repair ticket kind");
  Expect(c.priority == 99, "RemeshSeam urgency refreshed");
  Expect(c.column.x == 3 && c.column.y == 4, "repair ticket column");
  Expect(!s.Contains({3, 4}, ColumnWorkKind::RemeshSeam),
         "Contains false after drain");

  // Full-width coordinates must not collide (M02/A05).
  {
    UColumnFlowScheduler w;
    w.Enqueue({1, 0}, ColumnWorkKind::FirstMesh, 10);
    w.Enqueue({1, 65536}, ColumnWorkKind::FirstMesh, 20);
    Expect(w.LiveCount() == 2, "distinct full-width columns");
    Expect(w.ContainsColumn({1, 65536}), "far Z column retained");
  }

  // Sticky without mesh + far stale ⇒ FirstMesh / RelightThenMesh (ColPipe P1).
  {
    using cutum::EnqueueStickyStaleRepairTickets;
    UColumnFlowScheduler t;
    EnqueueStickyStaleRepairTickets(t, {0, 0}, {{1, 0}}, {{5, 0}});
    Expect(t.Contains({1, 0}, ColumnWorkKind::FirstMesh), "sticky FirstMesh");
    Expect(!t.Contains({1, 0}, ColumnWorkKind::RemeshSeam),
           "exclusive: sticky not RemeshSeam proxy");
    Expect(t.Contains({5, 0}, ColumnWorkKind::RelightThenMesh),
           "stale RelightThenMesh");
    Expect(!t.Contains({5, 0}, ColumnWorkKind::RemeshSeam),
           "stale RemeshSeam denied");
  }

  // Cruise SOTA: PromoteRelight coalesce = one ticket (scheduler exclusive +
  // executor RequestPromote max-prio flush). Scheduler side:
  {
    UColumnFlowScheduler flush_s;
    flush_s.Enqueue({2, 2}, ColumnWorkKind::PromoteRelight, 95);
    flush_s.Enqueue({2, 2}, ColumnWorkKind::PromoteRelight, 40);
    Expect(flush_s.Size() == 1, "PromoteRelight refresh one live ticket");
    ColumnWorkItem pr{};
    Expect(flush_s.DrainOne(pr), "drain PromoteRelight");
    Expect(pr.priority == 95, "Promote keeps max priority on refresh");
    flush_s.Enqueue({3, 3}, ColumnWorkKind::FirstMesh, 100);
    flush_s.Enqueue({3, 3}, ColumnWorkKind::PromoteRelight, 50);
    Expect(flush_s.Size() == 1, "Promote on FirstMesh denied (lower rank)");
    Expect(flush_s.DeniedCount() >= 1, "Promote on occupied column denied");
    Expect(flush_s.Contains({3, 3}, ColumnWorkKind::FirstMesh),
           "FirstMesh remains after denied Promote");
    Expect(!flush_s.Contains({3, 3}, ColumnWorkKind::PromoteRelight),
           "Promote did not replace FirstMesh");
    // FirstMesh upgrades Promote if Promote was first:
    UColumnFlowScheduler up_s;
    up_s.Enqueue({4, 4}, ColumnWorkKind::PromoteRelight, 50);
    up_s.Enqueue({4, 4}, ColumnWorkKind::FirstMesh, 100);
    Expect(up_s.UpgradeCount() >= 1, "Promote→FirstMesh upgrade");
    ColumnWorkItem p{};
    Expect(up_s.DrainOne(p), "drain upgraded FirstMesh");
    Expect(p.kind == ColumnWorkKind::FirstMesh, "FirstMesh after upgrade");
  }

  {
    UColumnFlowScheduler repeated;
    for (int n = 0; n < 10000; ++n)
      repeated.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 100);
    Expect(repeated.LiveCount() == 1 && repeated.HeapCount() == 1,
           "identical demand cannot grow tombstone heap");
  }

  // N06: structural cooldown keys must not collide on negative Z / adjacent X.
  {
    using cutum::UColumnFlowExecutor;
    using K = UColumnFlowExecutor::CooldownKey;
    const auto k = ColumnWorkKind::FirstMesh;
    Expect(!(UColumnFlowExecutor::MakeCooldownKey({-1, -1}, k) ==
             UColumnFlowExecutor::MakeCooldownKey({-2, -1}, k)),
           "N06: (-1,-1) != (-2,-1)");
    Expect(!(UColumnFlowExecutor::MakeCooldownKey({0, -1}, k) ==
             UColumnFlowExecutor::MakeCooldownKey({255, -1}, k)),
           "N06: (0,-1) != (255,-1)");
    Expect(!(UColumnFlowExecutor::MakeCooldownKey({1, 2}, ColumnWorkKind::FirstMesh) ==
             UColumnFlowExecutor::MakeCooldownKey({1, 2}, ColumnWorkKind::RemeshSeam)),
           "N06: kind distinguishes same column");
    Expect(UColumnFlowExecutor::MakeCooldownKey({INT32_MIN, INT32_MAX}, k) ==
               UColumnFlowExecutor::MakeCooldownKey({INT32_MIN, INT32_MAX}, k),
           "N06: int32 edges self-equal");
    std::unordered_map<K, int, UColumnFlowExecutor::CooldownKeyHash> map;
    map[UColumnFlowExecutor::MakeCooldownKey({-1, -1}, k)] = 1;
    map[UColumnFlowExecutor::MakeCooldownKey({-2, -1}, k)] = 2;
    Expect(map.size() == 2, "N06: map stores both negative-Z neighbors");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_flow_scheduler_test: OK\n";
  return EXIT_SUCCESS;
}
