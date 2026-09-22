// Audit-only. Exit 1 = violated invariants reproduced, not a harness failure.
// No game, user world, GL context or production source modifications.
#include <cstddef>
#include <iostream>
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Camera/GpuPassRefreshPolicy.h"
#include "Render/Mesh/MeshCaptureStore.h"
#include "Render/Mesh/MeshPublishContract.h"
#include "World/Core/BlockWorld.h"
#include "World/Streaming/RelightInstallPlanner.h"

int main()
{
  using namespace cutum;
  int violations = 0;
  UBlockWorld world;
  const glm::ivec3 c(0);
  world.GetChunkManager().EnsureChunk(c);
  for (int axis = 0; axis < 3; ++axis)
    for (int sign : {-1, 1}) {
      glm::ivec3 n(0); n[axis] = sign;
      world.GetChunkManager().EnsureChunk(n);
    }
  // Loaded AIR peer hidden by visual policy: full capture permits face;
  // incremental refresh marks same peer Unknown and suppresses it.
  auto hidden = +[](void *, glm::ivec3) { return false; };
  UMeshCaptureStore store;
  store.SetNeighborVisualDrawableFn(hidden, nullptr);
  auto full = store.CaptureAndStore(world, c, 1);
  if (!full) { std::cerr << "HARNESS: snapshot unavailable\n"; return 2; }
  const auto credit_before = UPipelineAdmission::Get().SnapshotPendingBytes();
  auto inc = store.RefreshIncrementalShell(world, c, 1, 1u << 1);
  if (!inc) { std::cerr << "HARNESS: refresh unavailable\n"; return 2; }
  const glm::ivec3 peer(CHUNK_SIZE, 0, 0);
  const bool mismatch = full->GetNeighborLoadState(peer) != inc->GetNeighborLoadState(peer);
  const bool stamp_valid = inc->InputsStillValid(world);
  std::cout << "same_world_full_vs_incremental_state_mismatch=" << mismatch
            << " full=" << int(full->GetNeighborLoadState(peer))
            << " incremental=" << int(inc->GetNeighborLoadState(peer))
            << " stamp_still_valid=" << stamp_valid << '\n';
  violations += mismatch && stamp_valid;
  const auto credit_after = UPipelineAdmission::Get().SnapshotPendingBytes();
  const bool uncredited = credit_before > 0 && credit_after == 0 && store.TryGet(world, c, 1).has_value();
  std::cout << "resident_snapshot_uncredited_after_refresh=" << uncredited
            << " before=" << credit_before << " after=" << credit_after << '\n';
  violations += uncredited;

  // Production planner predicate says force at >=8, caller guards !pending,
  // while predicate requires pending. Exercise actual planner function.
  ColumnChunkSnapshot chunk;
  chunk.fully_dark = true; chunk.has_drawable = true; chunk.is_dirty = true;
  chunk.gpu_pending = true; chunk.has_publish_progress = false;
  chunk.prefer_kick_stall_frames = 100;
  LitApplyPlan plan;
  TryPreferKickOrForceDirty(plan, chunk, true, true);
  bool stalled = plan.mark_dirty.empty() && plan.mark_dirty_priority.empty()
                 && plan.prefer_kick_gpu.empty() && plan.note_prefer_kick_stall;
  std::cout << "pending_no_progress_age100_only_notes_stall=" << stalled << '\n';
  violations += stalled;

  // Pass-aligned material stamps (A21-05): expected matches THIS PASS only.
  uint16_t opaque[] = {8};
  MeshPublishRevs expected{7, 11, MeshPublishMaterialStamp(opaque, 1)};
  MeshPublishRevs got{7, 11, MeshPublishMaterialStamp(opaque, 1)};
  const bool false_retain = !ShouldAcceptMaterialBlockIdFlip(got, expected, true, true);
  std::cout << "matching_mixed_material_chunk_rejected_by_pass_hash=" << false_retain << '\n';
  violations += false_retain;
  CullInputKey old_key;
  old_key.resultValid = true;
  CullInputKey new_key = old_key;
  new_key.viewProjHash = 1234; // camera rotation, no geometry change
  const bool cull_key_ok = CullInputKeyAllowsCacheReuse(old_key, new_key);
  const bool stale_cull = !cull_key_ok &&
      ShouldDeferOpaqueCompactCullForDeadline(0.0, true, false, false,
                                              cull_key_ok);
  std::cout << "deadline_allows_reuse_despite_changed_camera_key=" << stale_cull << '\n';
  violations += stale_cull;
  // Camera sort revision changed: skip must require sort_revision_unchanged.
  const bool stale_sort =
      ShouldSkipTransparentFullResort(true, true, 0,
                                      /*sort_revision_unchanged=*/false);
  std::cout << "stable_geometry_skips_sort_even_if_camera_sort_revision_changed=" << stale_sort << '\n';
  violations += stale_sort;
  std::cout << "violations=" << violations << '\n';
  return violations ? 1 : 0;
}
