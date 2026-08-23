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

  Expect(ShouldConsumeTicketedVbDebt(0, 81, 0), "consume VB debt");
  Expect(!ShouldConsumeTicketedVbDebt(10, 81, 0), "orphan nt blocks consume");

  if (failures != 0)
  {
    std::cerr << failures << " characterization test(s) failed" << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
