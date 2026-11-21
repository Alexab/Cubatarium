#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/ColumnRecord.h"

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
  {
    cutum::UColumnRecordStore records;
    const glm::ivec2 column{-7,9};
    auto &record=records.GetOrCreate(column);
    ColumnWorkItem a{}; a.cy=-3; a.priority=90; a.scan_full_focus=true;
    auto b=a; b.cy=2; b.priority=20; b.scan_full_focus=false;
    auto &intent=record.flow_intent[static_cast<size_t>(ColumnWorkKind::FirstMesh)];
    intent.Merge(a); intent.Merge(b);
    Expect(intent.pending && intent.cy==cutum::kColumnWorkWholeColumnCy &&
        intent.priority==90 && intent.scan_full_focus,"world intent retains both signed scopes and urgency");
    auto &relight=record.flow_intent[static_cast<size_t>(ColumnWorkKind::RelightThenMesh)];
    relight.Merge(a);
    intent={};
    Expect(!intent.pending && relight.pending,"admitting one kind preserves another pending intent");
    for(int n=0;n<10000;++n) relight.Merge(b);
    Expect(records.Size()==1,"repeated admission intent does not allocate overflow entries");
    records.Erase(column);
    const auto &replacement=records.GetOrCreate(column);
    bool empty=true;
    for(const auto &item:replacement.flow_intent) empty=empty && !item.pending;
    Expect(empty,"column retirement does not transfer old intent to replacement");
    records.Clear();
    Expect(records.Size()==0,"world clear retires admission intent with record");
  }
  {
    cutum::UColumnRecordStore records;
    for(int x=-70;x<70;++x) records.GetOrCreate({x,-9});
    std::optional<uint64_t> cursor;
    std::vector<glm::ivec2> keys;
    records.CollectKeysAfter(cursor,10000,keys);
    Expect(keys.size()==64,"record replay batch is capped at64 keys");
    const auto retired=keys.back();
    records.Erase(retired); // Cursor's exact key need not remain resident.
    records.GetOrCreate({1000,-9});
    std::unordered_map<uint64_t,int> seen;
    for(int batch=0;batch<3;++batch) {
      records.CollectKeysAfter(cursor,64,keys);
      for(const auto key:keys) ++seen[cutum::PackColumnKey(key)];
    }
    Expect(seen.size()==records.Size() && seen.count(cutum::PackColumnKey(retired))==0,
           "bounded cursor covers surviving and inserted keys after retirement");
    records.CollectKeysAfter(cursor,0,keys);
    Expect(keys.empty(),"zero replay budget visits no records");
    records.Clear(); records.GetOrCreate({-1,-2});
    records.CollectKeysAfter(cursor,64,keys);
    Expect(keys.size()==1 && keys.front()==glm::ivec2(-1,-2),
           "key cursor safely resumes after world store replacement");
  }
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

  {
    UColumnFlowScheduler refreshed;
    for(int n=0;n<2000;++n)
      refreshed.Enqueue({1,2},ColumnWorkKind::FirstMesh,n);
    Expect(refreshed.LiveCount()==1 && refreshed.HeapCount()==1,
           "changing urgency retains one ordered entry per live column");
    for(int n=0;n<2000;++n) {
      refreshed.RemoveColumn({1,2});
      refreshed.Enqueue({1,2},ColumnWorkKind::FirstMesh,n);
    }
    Expect(refreshed.LiveCount()==1 && refreshed.HeapCount()==1,
           "remove and reissue cannot accumulate retired entries");
    refreshed.RemoveColumn({1,2});
    Expect(refreshed.HeapCount()==0,"removal releases ordered storage immediately");
    refreshed.Enqueue({2,2},ColumnWorkKind::FirstMesh,7);
    refreshed.Enqueue({3,2},ColumnWorkKind::FirstMesh,9);
    refreshed.Enqueue({4,2},ColumnWorkKind::FirstMesh,9);
    ColumnWorkItem out{};
    Expect(refreshed.DrainOne(out) && out.column==glm::ivec2(3,2),"highest priority oldest tie drains first");
    Expect(refreshed.DrainOne(out) && out.column==glm::ivec2(4,2),"equal priority retains FIFO");
    Expect(refreshed.DrainOne(out) && out.column==glm::ivec2(2,2),"lower priority retained");
  }

  // Negative chunk coordinates are concrete targets, not range sentinels.
  {
    UColumnFlowScheduler signed_y;
    ColumnWorkItem exact{};
    exact.column = {5, 6};
    exact.kind = ColumnWorkKind::FirstMesh;
    exact.priority = 80;
    exact.cy = -1;
    signed_y.Enqueue(exact);
    ColumnWorkItem broad = exact;
    broad.cy = cutum::kColumnWorkWholeRemeshBandCy;
    broad.priority = 90;
    signed_y.Enqueue(broad);
    ColumnWorkItem drained{};
    Expect(signed_y.DrainOne(drained), "signed-Y work is drainable");
    Expect(drained.cy == cutum::kColumnWorkWholeColumnCy,
           "broad refresh retains negative slice and broad obligation");
  }

  // A single live ticket must retain both requested signed slices.
  for (int mode=0; mode<3; ++mode)
  {
    UColumnFlowScheduler merged;
    ColumnWorkItem old{}; old.column={-7,9}; old.cy=-3;
    old.kind=mode==1 ? ColumnWorkKind::RemeshSeam : ColumnWorkKind::FirstMesh;
    old.priority=90; old.scan_full_focus=true;
    merged.Enqueue(old);
    ColumnWorkItem next=old; next.cy=2; next.priority=20; next.scan_full_focus=false;
    next.kind=mode==2 ? ColumnWorkKind::RelightThenMesh : ColumnWorkKind::FirstMesh;
    if(mode==2) Expect(merged.ReplaceColumnTicket(next,old.kind),"explicit dependent replacement accepted");
    else merged.Enqueue(next);
    ColumnWorkItem out{};
    Expect(merged.DrainOne(out),"merged signed-slice request drains");
    Expect(out.cy==cutum::kColumnWorkWholeColumnCy,"coalescing preserves both signed slices");
    Expect(out.scan_full_focus && out.priority==90,"coalescing preserves scan extent and urgency");
    Expect(!merged.DrainOne(out),"superseded ticket cannot run again");
    merged.Enqueue(next);
    Expect(merged.RemoveColumn(next.column),"remove retires live ticket");
    merged.Enqueue(old);
    Expect(merged.DrainOne(out) && out.cy==-3,"same-coordinate successor survives old tombstones");
    merged.Enqueue(next); merged.Clear();
    merged.Enqueue(old);
    Expect(merged.DrainOne(out) && out.cy==-3 && !merged.DrainOne(out),"clear retires old ticket without poisoning successor");
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
