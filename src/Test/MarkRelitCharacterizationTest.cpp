#include "World/Streaming/RelightInstallPlanner.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/MeshLightStalePolicy.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Lighting/ChunkRelightSnapshot.h"

#include <cstdlib>
#include <iostream>

static int failures = 0;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    ++failures;
  }
}

int main()
{
  using cutum::ClassifyColumnInstallPath;
  using cutum::ColumnInstallPath;
  using cutum::LitApplyColumnInput;
  using cutum::PlanColumnInstall;
  using cutum::ColumnChunkSnapshot;
  using cutum::ShouldConsumeTicketedVbDebt;
  using cutum::ShouldForceMarkRelitForTicketedStale;
  using cutum::IsMeshLightStale;
  using cutum::PrimaryLightUnchanged;

  // C01 consume + primary + finalize
  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = true;
    in.primary_only = true;
    in.consume_mode = true;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::PrimaryConsume,
           "C01 PrimaryConsume");
  }

  // C02 partial
  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = false;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::PartialNoDirty,
           "C02 PartialNoDirty");
    const auto plan = PlanColumnInstall(in);
    Expect(plan.mark_dirty.empty() && plan.mark_dirty_priority.empty(),
           "C02 no MarkDirty");
  }

  // C03 enter gate
  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = true;
    in.enter_gate = true;
    in.enter_quiesce = false;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::PrimaryEnter,
           "C03 PrimaryEnter");
  }

  // C04 enter quiesce
  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = true;
    in.enter_quiesce = true;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::PrimaryQuiesce,
           "C04 PrimaryQuiesce");
  }

  // C06 PrimaryLightUnchanged
  {
    std::array<uint8_t, cutum::CHUNK_VOLUME> a{};
    std::array<uint8_t, cutum::CHUNK_VOLUME> b{};
    a.fill(1);
    b.fill(1);
    Expect(PrimaryLightUnchanged(a, b), "C06 unchanged light");
    b[0] = 2;
    Expect(!PrimaryLightUnchanged(a, b), "C06 changed light");
  }

  // C09 force stale ticket
  Expect(ShouldForceMarkRelitForTicketedStale(true, true, true, true, 2),
         "C09 force stale ticket");
  Expect(!ShouldForceMarkRelitForTicketedStale(true, true, true, true, 5),
         "C09 rim outside ring");

  // C13 stale revision
  Expect(IsMeshLightStale(1, 2), "C13 stale revision");
  Expect(!IsMeshLightStale(2, 2), "C14 fresh revision");

  // C16 neighbor primary_only skip
  {
    LitApplyColumnInput in{};
    in.is_primary = false;
    in.primary_only = true;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::Skip,
           "C16 neighbor skip");
  }

  // C17 primary_only cruise slim (defer without enter)
  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = true;
    in.primary_only = true;
    in.consume_mode = false;
    in.enter_gate = false;
    in.enter_quiesce = false;
    Expect(ClassifyColumnInstallPath(in) == ColumnInstallPath::PrimaryConsume,
           "C17 primary_only cruise slim");
  }

  {
    LitApplyColumnInput in{};
    in.is_primary = true;
    in.finalize_gate = true;
    in.primary_only = true;
    in.consume_mode = true;
    in.focus_horiz = 5;
    ColumnChunkSnapshot ch{};
    ch.coord = {1, 0, 0};
    ch.is_dirty = true;
    ch.fully_dark = true;
    ch.has_drawable = true;
    ch.meshed_light_rev = 1;
    ch.light_field_rev = 3;
    in.relit_chunks.push_back(ch);
    const auto plan = PlanColumnInstall(in);
    Expect(!plan.mark_dirty_priority.empty(),
           "P6: consume FullyDark already-dirty still priority Dirty");
    Expect(plan.schedule_n >= 1, "P6: consume hole schedules");
    ch.light_field_rev = 1;
    in.relit_chunks.clear();
    in.relit_chunks.push_back(ch);
    const auto plan_noop = PlanColumnInstall(in);
    Expect(plan_noop.mark_dirty_priority.empty(),
           "P7: matching light rev does not bump FullyDark remesh");
  }

  Expect(ShouldConsumeTicketedVbDebt(0, 81, 0), "consume VB debt");
  Expect(ShouldConsumeTicketedVbDebt(6, 107, 0), "P1 soft nt<=8 consume");
  Expect(!ShouldConsumeTicketedVbDebt(10, 81, 0), "orphan nt>8 blocks consume");
  {
    using cutum::ShouldConsumeUnlitTicketedVbStand;
    Expect(ShouldConsumeUnlitTicketedVbStand(false, 107, 6, 62, 61),
           "P1 unlit≈PL stand consume");
    Expect(!ShouldConsumeUnlitTicketedVbStand(false, 107, 20, 62, 61),
           "P1 high no_ticket blocks unlit consume");
  }

  if (failures != 0)
  {
    std::cerr << failures << " characterization test(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
