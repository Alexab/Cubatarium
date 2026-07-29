#include "World/Streaming/ColumnFlowScheduler.h"

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
  Expect(s.Size() == 2, "different kind not deduped");
  ColumnWorkItem a{}, b{};
  Expect(s.DrainOne(a), "drain first");
  Expect(a.priority == 10, "higher priority first (10 > 5)");
  Expect(a.column.x == 1 && a.column.y == 2, "column coords preserved");
  Expect(a.kind == ColumnWorkKind::FirstMesh, "kind FirstMesh");
  Expect(s.DrainOne(b), "drain second");
  Expect(b.kind == ColumnWorkKind::RelightThenMesh, "kind Relight");
  Expect(b.column.x == 1 && b.column.y == 2, "column on second item");
  Expect(!s.DrainOne(a), "empty");

  // TD-ARCH-026: RemeshSeam is the hide=>repair ticket kind.
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 30);
  s.Enqueue({3, 4}, ColumnWorkKind::RemeshSeam, 99);
  Expect(s.Size() == 1, "RemeshSeam ticket deduped");
  ColumnWorkItem c{};
  Expect(s.DrainOne(c), "drain RemeshSeam");
  Expect(c.kind == ColumnWorkKind::RemeshSeam, "repair ticket kind");
  Expect(c.column.x == 3 && c.column.y == 4, "repair ticket column");

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_flow_scheduler_test: OK\n";
  return EXIT_SUCCESS;
}
