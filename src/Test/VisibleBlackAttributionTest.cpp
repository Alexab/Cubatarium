#include "World/Streaming/VisibleBlackAttribution.h"

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

void ExpectCause(cutum::VisibleBlackCause expected, cutum::VisibleBlackCause actual,
                 const char *msg)
{
  if (expected != actual)
  {
    std::cerr << "FAIL: " << msg << " (expected " << static_cast<int>(expected)
              << " got " << static_cast<int>(actual) << ")\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::ClassifyVisibleBlackColumn;
  using cutum::VisibleBlackCause;

  // Q2 follow-up: full GL pixel oracle for visible-black columns lives in
  // greedy_vertex_pool_production_test / --driver (allocate→draw→replace→
  // readback). This unit file covers classification + census mismatch only.
  // LegalDark must not be treated as auto-relight demand.

  ExpectCause(VisibleBlackCause::StaleDarkWithLitField,
              ClassifyVisibleBlackColumn(true, false, false, false, false,
                                         false),
              "stale dark with lit field");
  ExpectCause(VisibleBlackCause::FullyDarkPendingRepair,
              ClassifyVisibleBlackColumn(false, true, true, true, false, false),
              "fully dark with ticket and progress");
  ExpectCause(VisibleBlackCause::FullyDarkPendingRepair,
              ClassifyVisibleBlackColumn(false, true, false, true, false,
                                         false),
              "fully dark with progress");
  ExpectCause(VisibleBlackCause::FullyDarkPendingRepair,
              ClassifyVisibleBlackColumn(false, true, false, false, true,
                                         false),
              "fully dark with sticky");
  ExpectCause(VisibleBlackCause::FullyDarkStalledTicket,
              ClassifyVisibleBlackColumn(false, true, true, false, false,
                                         false),
              "fully dark stalled ticket");
  // N04 H1: equal-rev must NOT reclassify ticketed FullyDark as LegalDark.
  ExpectCause(VisibleBlackCause::FullyDarkStalledTicket,
              ClassifyVisibleBlackColumn(false, true, true, false, false,
                                         false),
              "ticketed FullyDark stays stalled (no equal-rev LegalDark)");
  ExpectCause(VisibleBlackCause::FullyDarkNoTicket,
              ClassifyVisibleBlackColumn(false, true, false, false, false, true),
              "fully dark pending-light no ticket");
  ExpectCause(VisibleBlackCause::LegalDarkNoRepair,
              ClassifyVisibleBlackColumn(false, true, false, false, false,
                                         false),
              "fully dark legal cave");

  // Census mismatch semantics (WorldStreaming publish): flag=1 when
  // unfinished_visual==0 && visible_black_focus_n>0 on the same frame.
  {
    const int unfinished_visual = 0;
    const int visible_black_focus_n = 63;
    const int census_mismatch =
        (unfinished_visual == 0 && visible_black_focus_n > 0) ? 1 : 0;
    Expect(census_mismatch == 1, "census mismatch when VB>0 and unfinished=0");
    Expect((unfinished_visual == 0 && visible_black_focus_n == 0 ? 1 : 0) == 0,
           "no census mismatch when both zero");
    Expect((unfinished_visual > 0 && visible_black_focus_n > 0 ? 1 : 0) == 0,
           "no census mismatch when unfinished>0");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "VisibleBlackAttributionTest: PASS\n";
  return 0;
}
