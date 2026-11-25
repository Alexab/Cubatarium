#include "World/Lighting/AsyncRelightBuilder.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightRetryQueue.h"
#include "Core/Jobs/PipelineAdmission.h"
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <fstream>

namespace cutum
{
std::promise<void> *RunningEntered = nullptr;
std::shared_future<void> RunningRelease;
std::atomic<bool> RunningHookClaimed{false};
// The seam controls scheduling only; cancellation and debt delivery are real.
struct AsyncRelightLifecycleTestAccess
{
  static void FillPendingSlot(UAsyncRelightBuilder &builder)
  {
    builder.Pool.SetMaxPendingJobCount(1);
    builder.Pool.Enqueue([]{});
  }
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

#include "Test/CatalogRelightQueueOwners.h"

int main(int argc,char **argv)
{
  using namespace cutum;
  using namespace std::chrono_literals;
  // Environment flags are process-constant and cached on first use.
  _putenv_s("CUBATARIUM_RELIGHT_AUDIT", "1");
  if(argc==3 && std::string(argv[1])=="--catalog-queue-owners")return TestCatalogRelightQueueOwners(argv[2]);
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
  RelightRetryQueue deferred;
  deferred.Append(recovered);
  deferred.Suspend();
  int submissions = 0;
  auto submit = [&](const RelightJobSpec &request) {
    builder.EnqueueJob(world, request, registry);
    ++submissions;
    return true;
  };
  deferred.Drain(8, submit);
  if (submissions || deferred.Size() != 1 || builder.HasPendingWork()) return 1;
  deferred.Resume();
  deferred.Drain(0, submit);
  deferred.Drain(1, [](const RelightJobSpec &) { return false; });
  if (submissions || deferred.Size() != 1) return 1;
  deferred.Drain(1, submit);
  if (submissions != 1 || !deferred.Empty()) return 1;
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
  deferred.Append({spec, second});
  deferred.Drain(1, [](const RelightJobSpec &) { return true; });
  if (deferred.Size() != 1) return 1;
  // Replacement world retires old obligations instead of replaying them.
  deferred.Clear();
  deferred.Drain(8, submit);
  if (!deferred.Empty() || submissions != 1) return 1;
  spec.job_id=9201;
  UPipelineAdmission::Get().SetWorkSlotCap(0);
  builder.EnqueueJob(world,spec,registry);
  if(!builder.WaitIdleFor(5s)) return 2;
  const bool debt_visible=builder.HasPendingWork();
  auto refused=builder.TakeDiscardedJobs();
  const bool refused_exact=debt_visible && refused.size()==1 && SameRequest(refused.front(),spec) &&
      !builder.HasInFlightJob(spec.job_id) && builder.GetCompletedSize()==0 &&
      builder.GetQueuedJobCount()==0 && UPipelineAdmission::Get().WorkSlotsInUse()==0;
  UPipelineAdmission::Get().SetWorkSlotCap(8);
  if(!refused_exact) {
    std::cerr << "FAIL: denied relight work slot bypassed admission or lost exact retry\n";
    return 1;
  }
  builder.EnqueueJob(world,refused.front(),registry);
  if(!builder.WaitIdleFor(5s)) return 2;
  auto admitted=builder.DrainCompleted(1);
  if(admitted.size()!=1 || !SameRequest(admitted.front().retry_spec,spec) ||
      !builder.TakeDiscardedJobs().empty() || UPipelineAdmission::Get().WorkSlotsInUse()!=0) return 1;
  std::cout << "relight_work_slot_refusal exact_retry=1 no_bypass=1 successor=1 PASS\n";
  std::promise<void> pool_entered,pool_release;
  auto pool_started=pool_entered.get_future();
  AsyncRelightLifecycleTestAccess::BlockWorker(builder,pool_entered,pool_release.get_future().share());
  if(pool_started.wait_for(5s)!=std::future_status::ready) { pool_release.set_value(); return 2; }
  AsyncRelightLifecycleTestAccess::FillPendingSlot(builder);
  spec.job_id=9202;
  builder.EnqueueJob(world,spec,registry);
  const auto pool_refused=builder.TakeDiscardedJobs();
  const bool pool_exact=pool_refused.size()==1 && SameRequest(pool_refused.front(),spec) &&
      !builder.HasInFlightJob(spec.job_id) && builder.GetQueuedJobCount()==1 &&
      UPipelineAdmission::Get().WorkSlotsInUse()==0;
  pool_release.set_value();
  if(!builder.WaitIdleFor(5s)) return 2;
  if(!pool_exact) { std::cerr << "FAIL: relight pool refusal loses request or leaks owner\n"; return 1; }
  builder.CancelPending();
  if(!builder.TakeDiscardedJobs().empty()) return 1;
  builder.EnqueueJob(world,pool_refused.front(),registry);
  if(!builder.WaitIdleFor(5s)) return 2;
  auto pool_successor=builder.DrainCompleted(1);
  if(pool_successor.size()!=1 || !SameRequest(pool_successor.front().retry_spec,spec)) return 1;
  spec.job_id=9203;
  UPipelineAdmission::Get().SetWorkSlotCap(0);
  builder.Enqueue(UChunkRelightSnapshot::Capture(world,spec),registry);
  const auto legacy_refused=builder.TakeDiscardedJobs();
  UPipelineAdmission::Get().SetWorkSlotCap(8);
  if(legacy_refused.size()!=1 || !SameRequest(legacy_refused.front(),spec) ||
      builder.HasPendingWork() || UPipelineAdmission::Get().WorkSlotsInUse()!=0) return 1;
  std::cout << "relight_pool_refusal exact_retry=1 no_ghost=1 successor=1 legacy_no_bypass=1 PASS\n";
  std::cout << "PASS: queued/completed cancel and overflow retain exact requests; "
               "successor validates and installs AIR field; running cancel "
               "late completion does not retire same-ID successor\n";
  return 0;
}
