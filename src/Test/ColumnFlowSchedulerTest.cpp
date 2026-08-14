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
  Expect(s.Size() == 1, "competing kind on same column denied");
  Expect(s.ContainsColumn({1, 2}), "column occupied");
  Expect(s.DeniedCount() >= 1, "denied competing producer");
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

  // Sticky without mesh + far stale ⇒ RemeshSeam|RelightThenMesh in scheduler.
  {
    using cutum::EnqueueStickyStaleRepairTickets;
    UColumnFlowScheduler t;
    EnqueueStickyStaleRepairTickets(t, {0, 0}, {{1, 0}}, {{5, 0}});
    Expect(t.Contains({1, 0}, ColumnWorkKind::RemeshSeam), "sticky RemeshSeam");
    Expect(!t.Contains({1, 0}, ColumnWorkKind::RelightThenMesh),
           "exclusive: sticky not dual Relight");
    Expect(t.Contains({5, 0}, ColumnWorkKind::RemeshSeam), "stale RemeshSeam");
    Expect(!t.Contains({5, 0}, ColumnWorkKind::RelightThenMesh),
           "stale RelightThenMesh denied");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_flow_scheduler_test: OK\n";
  return EXIT_SUCCESS;
}
