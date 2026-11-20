#include "World/Lighting/AsyncRelightBuilder.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Lighting/RelightInputValidation.h"
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>

namespace cutum
{
std::promise<void> *RunningEntered = nullptr;
std::shared_future<void> RunningRelease;
std::atomic<bool> RunningHookClaimed{false};
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
void CubatariumLogInfo(const char *, const std::string &message)
{
  if (RunningEntered && message == "worker_start job=9106" &&
      !RunningHookClaimed.exchange(true))
  {
    RunningEntered->set_value();
    RunningRelease.wait();
  }
}
}

namespace
{
bool SameRequest(const cutum::RelightJobSpec &a,
                 const cutum::RelightJobSpec &b)
{
  return a.job_id == b.job_id && a.block_positions == b.block_positions &&
         a.min_world_y == b.min_world_y && a.max_world_y == b.max_world_y &&
         a.include_block_light == b.include_block_light &&
         a.include_skylight == b.include_skylight &&
         a.frontier_iterations == b.frontier_iterations &&
         a.finalize_pending_gate == b.finalize_pending_gate &&
         a.visible_draw_gate_repair == b.visible_draw_gate_repair &&
         a.column_center_only == b.column_center_only &&
         a.include_known_air_outputs == b.include_known_air_outputs &&
         a.known_air_output_sections == b.known_air_output_sections &&
         a.light_sample_output_sections == b.light_sample_output_sections;
}
}

int main()
{
  using namespace cutum;
  using namespace std::chrono_literals;
  // Environment flags are process-constant and cached on first use.
  _putenv_s("CUBATARIUM_RELIGHT_AUDIT", "1");
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
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
  spec.light_sample_output_sections = {{0, -3, 0}};
  spec.include_known_air_outputs = true;
  spec.column_center_only = true;
  spec.visible_draw_gate_repair = true;
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
  auto recovered = builder.TakeDiscardedJobs();
  if (recovered.size() != 1 || !SameRequest(recovered.front(), spec) ||
      !builder.TakeDiscardedJobs().empty())
  {
    std::cerr << "FAIL: queued cancel did not return exact obligation once\n";
    return 1;
  }
  // Recapture and run the real successor, then validate and install its output.
  recovered.front().job_id = 9102;
  builder.EnqueueJob(world, recovered.front(), registry);
  if (!builder.WaitIdleFor(5s)) return 2;
  auto results = builder.DrainCompleted(1);
  if (results.size() != 1 || !results.front().inputs_complete ||
      !SameRequest(results.front().retry_spec, recovered.front()) ||
      ValidateRelightInputs(results.front(), world,
          registry.GetDefinitionsCatalogSnapshot(), true) !=
          RelightInputValidation::Current || results.front().chunks.empty())
  {
    std::cerr << "FAIL: cancelled request successor not current/complete\n";
    return 1;
  }
  for (const auto &chunk : results.front().chunks)
    InstallComputedLight(world, chunk, false, true);
  if (!world.GetChunkManager().GetSectionLightField({0, -3, 0}))
  {
    std::cerr << "FAIL: successor did not publish requested AIR field\n";
    return 1;
  }
  builder.CancelPending();
  if (!builder.TakeDiscardedJobs().empty()) return 1;

  spec.job_id = 9103;
  builder.EnqueueJob(world, spec, registry);
  if (!builder.WaitIdleFor(5s) || builder.GetCompletedSize() != 1) return 2;
  builder.CancelPending();
  recovered = builder.TakeDiscardedJobs();
  if (recovered.size() != 1 || !SameRequest(recovered.front(), spec) ||
      !builder.DrainCompleted(8).empty() ||
      !builder.TakeDiscardedJobs().empty()) return 1;

  builder.SetCompletedCapacity(1);
  spec.job_id = 9104;
  builder.EnqueueJob(world, spec, registry);
  if (!builder.WaitIdleFor(5s)) return 2;
  auto second = spec;
  second.job_id = 9105;
  builder.EnqueueJob(world, second, registry);
  if (!builder.WaitIdleFor(5s)) return 2;
  recovered = builder.TakeOverflowJobs();
  if (recovered.size() != 1 || !SameRequest(recovered.front(), spec) ||
      !builder.TakeOverflowJobs().empty()) return 1;
  builder.CancelPending();
  recovered = builder.TakeDiscardedJobs();
  if (recovered.size() != 1 || !SameRequest(recovered.front(), second)) return 1;

  // Pause the actual worker between dispatch and Compute using its audit hook.
  // The successor deliberately reuses the ID across epochs (ABA control).
  builder.SetCompletedCapacity(8);
  std::promise<void> running_entered, running_release;
  auto running_started = running_entered.get_future();
  RunningEntered = &running_entered;
  RunningRelease = running_release.get_future().share();
  spec.job_id = 9106;
  builder.EnqueueJob(world, spec, registry);
  if (running_started.wait_for(5s) != std::future_status::ready)
  {
    running_release.set_value();
    builder.WaitIdleFor(5s);
    return 2;
  }
  builder.CancelPending();
  recovered = builder.TakeDiscardedJobs();
  const bool exact_running = recovered.size() == 1 &&
                             SameRequest(recovered.front(), spec);
  // The claimed audit hook does not block the successor.
  builder.EnqueueJob(world, spec, registry);
  running_release.set_value();
  if (!builder.WaitIdleFor(5s)) return 2;
  RunningEntered = nullptr;
  RunningRelease = {};
  const bool successor_tracked = builder.HasInFlightJob(spec.job_id);
  results = builder.DrainCompleted(8);
  if (!exact_running || !successor_tracked || results.size() != 1 ||
      results.front().submitEpoch != builder.SubmitEpoch() ||
      !SameRequest(results.front().retry_spec, spec) ||
      !builder.TakeDiscardedJobs().empty() || builder.HasPendingWork())
  {
    std::cerr << "FAIL: late completion duplicated debt or retired successor\n";
    return 1;
  }
  std::cout << "PASS: queued/completed cancel and overflow retain exact requests; "
               "successor validates and installs AIR field; running cancel "
               "late completion does not retire same-ID successor\n";
  return 0;
}
