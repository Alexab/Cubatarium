#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnRenderablePolicy.h"

#include <cstdlib>
#include <iostream>

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
  s.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 99); // dedupe
  Expect(s.Size() == 1, "dedupe same column+kind");
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
  Expect(a.priority == 10, "kept first ticket");
  Expect(a.kind == ColumnWorkKind::FirstMesh, "kind FirstMesh");
  Expect(!s.ContainsColumn({1, 2}), "column free after drain");
  Expect(!s.DrainOne(a), "empty");

  // TD-ARCH-026: RemeshSeam is the hide=>repair ticket kind.
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 30);
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 99);
  Expect(s.Size() == 1, "RemeshSeam ticket deduped");
  Expect(s.Contains({3, 4}, ColumnWorkKind::RemeshSeam),
         "Contains RemeshSeam while queued");
  ColumnWorkItem c{};
  Expect(s.DrainOne(c), "drain RemeshSeam");
  Expect(c.kind == ColumnWorkKind::RemeshSeam, "repair ticket kind");
  Expect(c.column.x == 3 && c.column.y == 4, "repair ticket column");
  Expect(!s.Contains({3, 4}, ColumnWorkKind::RemeshSeam),
         "Contains false after drain");

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
    Expect(flush_s.Size() == 1, "PromoteRelight dedupe one ticket");
    flush_s.Enqueue({3, 3}, ColumnWorkKind::FirstMesh, 100);
    flush_s.Enqueue({3, 3}, ColumnWorkKind::PromoteRelight, 50);
    Expect(flush_s.Size() == 2, "Promote on FirstMesh denied (lower rank)");
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

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_flow_scheduler_test: OK\n";
  return EXIT_SUCCESS;
}
