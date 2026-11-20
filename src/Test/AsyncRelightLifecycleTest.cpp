#include "World/Lighting/AsyncRelightBuilder.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <chrono>
#include <future>
#include <iostream>

namespace cutum
{
// The seam controls scheduling only; cancellation and debt delivery are real.
struct AsyncRelightLifecycleTestAccess
{
  static void BlockWorker(UAsyncRelightBuilder &builder,
                          std::promise<void> &entered,
                          std::shared_future<void> release)
  {
    builder.Pool.Enqueue([&entered, release] {
      entered.set_value();
      release.wait();
    });
  }
};
void CubatariumLogInfo(const char *, const std::string &) {}
}

int main()
{
  using namespace cutum;
  using namespace std::chrono_literals;
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  UAsyncRelightBuilder builder(1);
  std::promise<void> entered, release;
  auto started = entered.get_future();
  AsyncRelightLifecycleTestAccess::BlockWorker(
      builder, entered, release.get_future().share());
  if (started.wait_for(5s) != std::future_status::ready)
  {
    release.set_value();
    std::cerr << "FAIL: scheduling barrier timeout\n";
    return 2;
  }
  RelightJobSpec spec;
  spec.job_id = 9101;
  spec.block_positions = {{4, -40, 4}};
  spec.min_world_y = -48;
  spec.max_world_y = -33;
  spec.include_skylight = false;
  spec.finalize_pending_gate = false;
  spec.known_air_output_sections = {{1, -3, 0}};
  builder.EnqueueJob(world, spec, registry);
  const bool queued = builder.GetQueuedJobCount() == 1 &&
                      builder.HasInFlightJob(spec.job_id);
  builder.CancelPending();
  release.set_value();
  if (!queued || !builder.WaitIdleFor(5s))
  {
    std::cerr << "FAIL: queued cancellation precondition or idle timeout\n";
    return 2;
  }
  const auto recovered = builder.TakeDiscardedSourcePositions();
  if (recovered != spec.block_positions)
  {
    std::cerr << "FAIL: queued cancel lost relight obligation: expected_sources="
              << spec.block_positions.size() << " recovered_sources="
              << recovered.size() << '\n';
    return 1;
  }
  std::cout << "PASS: queued cancellation returns relight obligation\n";
  return 0;
}
