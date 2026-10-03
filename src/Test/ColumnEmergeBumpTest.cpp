#include "World/Streaming/ColumnEmergeBump.h"

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
  using cutum::ColumnEmergeBumpResult;
  using cutum::ColumnEmergeState;
  using cutum::ColumnWorkKind;
  using cutum::ColumnWorkKindExclusiveRank;
  using cutum::TryAcquireColumnEmergeBump;

  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::Empty,
                                    ColumnEmergeState::Lighting) ==
             ColumnEmergeBumpResult::Applied,
         "forward Empty→Lighting");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::Lighting,
                                    ColumnEmergeState::Lighting) ==
             ColumnEmergeBumpResult::Noop,
         "same state is noop (second Lighting producer)");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::Lighting,
                                    ColumnEmergeState::LitReady) ==
             ColumnEmergeBumpResult::Applied,
         "forward Lighting→LitReady");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::RenderReady,
                                    ColumnEmergeState::Meshing) ==
             ColumnEmergeBumpResult::Applied,
         "repair RenderReady→Meshing");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::RenderReady,
                                    ColumnEmergeState::Lighting) ==
             ColumnEmergeBumpResult::Applied,
         "repair RenderReady→Lighting");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::Meshing,
                                    ColumnEmergeState::LitReady) ==
             ColumnEmergeBumpResult::Noop,
         "LitReady finalize after Meshing is noop");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::RenderReady,
                                    ColumnEmergeState::LitReady) ==
             ColumnEmergeBumpResult::Noop,
         "LitReady finalize after RenderReady is noop");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::Meshing,
                                    ColumnEmergeState::Generating) ==
             ColumnEmergeBumpResult::Denied,
         "illegal Meshing→Generating denied");
  Expect(TryAcquireColumnEmergeBump(ColumnEmergeState::LitReady,
                                    ColumnEmergeState::Empty) ==
             ColumnEmergeBumpResult::Denied,
         "illegal LitReady→Empty denied");
  Expect(ColumnWorkKindExclusiveRank(ColumnWorkKind::FirstMesh) >
             ColumnWorkKindExclusiveRank(ColumnWorkKind::RelightThenMesh),
         "FirstMesh outranks Relight");
  Expect(ColumnWorkKindExclusiveRank(ColumnWorkKind::RelightThenMesh) >
             ColumnWorkKindExclusiveRank(ColumnWorkKind::RemeshSeam),
         "Relight outranks Remesh");

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "column_emerge_bump_test: OK\n";
  return EXIT_SUCCESS;
}
