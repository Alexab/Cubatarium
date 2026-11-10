#include "Core/Progress/IUProgressSink.h"

#include <cmath>
#include <iostream>

namespace
{
void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "progress_snapshot_test: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main()
{
  using namespace cutum;

  ULatestProgressSink sink;

  // Begin: indeterminate until a determinate report arrives.
  sink.Begin(WorldOperationKind::Load);
  Expect(sink.Get().fraction < 0.f, "begin starts indeterminate");
  Expect(!sink.Get().active || sink.Get().active, "snapshot active");
  Expect(sink.Get().subDone == -1 && sink.Get().subTotal == -1,
         "no substages at begin");

  // Substage fields pass through.
  sink.Report("chunks", 0.25f, "Loading chunks (4/16)...", 4, 16);
  Expect(sink.Get().subDone == 4 && sink.Get().subTotal == 16,
         "substage counters stored");
  Expect(std::fabs(sink.Get().fraction - 0.25f) < 1e-5f,
         "first determinate fraction stored");

  // Monotonic clamp: regression does not move the bar backwards.
  sink.Report("mesh_warmup", 0.40f, "Building meshes...", 100, 465);
  sink.Report("mesh_warmup", 0.31f, "Building meshes... (re-dirty)", 80, 465);
  Expect(std::fabs(sink.Get().fraction - 0.40f) < 1e-5f,
         "fraction never regresses on screen");

  // Determinate after indeterminate returns; indeterminate passes through.
  sink.Report("metadata", -1.f, "Loading world data...");
  Expect(sink.Get().fraction < 0.f, "indeterminate passes through");
  sink.Report("chunks", 0.5f, "half", 8, 16);
  Expect(std::fabs(sink.Get().fraction - 0.5f) < 1e-5f,
         "determinate resumes from the monotonic max");

  // Begin resets the monotonic memory.
  sink.Begin(WorldOperationKind::Create);
  sink.Report("init", 0.02f, "start");
  Expect(std::fabs(sink.Get().fraction - 0.02f) < 1e-5f,
         "new operation clamps from zero again");

  // End preserves the last message and flags the outcome.
  sink.End(true);
  Expect(!sink.Get().active && sink.Get().succeeded, "end marks success");

  std::cout << "progress_snapshot_test: OK\n";
  return 0;
}
