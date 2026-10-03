// Audit-only contract probes. Not linked into the game.
// Exit 1 means at least one expected scheduler contract was violated.
#include "World/Streaming/ColumnFlowScheduler.h"

#include <iostream>

using namespace cutum;

int main()
{
  int violations = 0;
  const auto check = [&violations](bool ok, const char *name) {
    std::cout << (ok ? "PASS: " : "VIOLATION: ") << name << '\n';
    violations += !ok;
  };
  ColumnWorkItem out{};
  {
    UColumnFlowScheduler queue;
    queue.Enqueue({1, 0}, ColumnWorkKind::FirstMesh, 10);
    queue.Enqueue({1, 65536}, ColumnWorkKind::FirstMesh, 20);
    check(queue.Size() == 2 && queue.ContainsColumn({1, 65536}),
          "distinct full-width column coordinates retain distinct work");
  }
  {
    UColumnFlowScheduler queue;
    ColumnWorkItem item{};
    item.column = {1, 2};
    item.kind = ColumnWorkKind::FirstMesh;
    item.priority = 10;
    item.cy = 1;
    queue.Enqueue(item);
    item.priority = 99;
    item.cy = 7;
    queue.Enqueue(item);
    const bool got = queue.DrainOne(out);
    std::cout << "priority refresh: got=" << got
              << " priority=" << out.priority << " cy=" << out.cy << '\n';
    check(got && out.priority == 99 && out.cy == 7,
          "same-kind urgency and preferred slice are refreshed");
  }
  {
    UColumnFlowScheduler queue;
    queue.Enqueue({1, 2}, ColumnWorkKind::RemeshSeam, 10);
    queue.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 20);
    check(queue.Size() == 1,
          "reported live queue depth excludes superseded tickets");
    queue.DrainOne(out); // FirstMesh wins over the old remesh.
    queue.Enqueue({1, 2}, ColumnWorkKind::RemeshSeam, 30);
    const bool got = queue.DrainOne(out);
    std::cout << "ABA replacement: got=" << got
              << " priority=" << out.priority << '\n';
    check(got && out.priority == 30,
          "old cancellation cannot discard a newer ticket with the same key");
  }
  {
    UColumnFlowScheduler queue;
    queue.Enqueue({1, 2}, ColumnWorkKind::RemeshSeam, 10);
    queue.Enqueue({1, 2}, ColumnWorkKind::FirstMesh, 20);
    queue.DrainOne(out);
    queue.DrainOne(out); // Consume cancelled old false variant.
    ColumnWorkItem item{};
    item.column = {1, 2};
    item.kind = ColumnWorkKind::RemeshSeam;
    item.priority = 30;
    item.scan_full_focus = true;
    queue.Enqueue(item);
    const bool got = queue.DrainOne(out);
    std::cout << "unused tombstone: got=" << got
              << " occupied=" << queue.ContainsColumn({1, 2}) << '\n';
    check(got && !queue.ContainsColumn({1, 2}),
          "cancelling a nonexistent variant cannot poison a future ticket");
  }
  std::cout << "contract violations=" << violations << '\n';
  return violations == 0 ? 0 : 1;
}
