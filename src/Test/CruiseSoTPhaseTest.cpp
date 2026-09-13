#include "World/Chunks/BlockQuery.h"
#include "World/Streaming/ColumnRecord.h"
#include "World/Streaming/ColumnRecordCoordinator.h"
#include "World/Streaming/ColumnTicketMap.h"
#include "World/Streaming/WorldBorderPolicy.h"

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
  using namespace cutum;

  // --- BlockQuery ---
  Expect(MakeUnloadedQuery().IsUnloaded(), "unloaded query");
  Expect(!MakeUnloadedQuery().IsAir(), "unloaded is not air");
  Expect(MakeAirQuery().IsAir(), "air query");
  Expect(MakeSolidQuery(BLOCK_AIR).IsAir(), "solid AIR folds to air");
  Expect(MakeSolidQuery(static_cast<BlockId>(1)).IsSolid(), "solid id");

  // --- WorldBorder ---
  WorldBorderConfig cfg;
  cfg.soft_half_extent = 1000.0f;
  cfg.soft_margin = 100.0f;
  cfg.hard_half_extent = 100000.0f;
  Expect(IsInsideSoftWorldBorder(glm::vec3(0, 0, 0), cfg), "origin inside soft");
  Expect(!IsInsideSoftWorldBorder(glm::vec3(2000, 0, 0), cfg), "far outside soft");
  glm::vec3 p(1500.0f, 64.0f, 0.0f);
  Expect(ClampToSoftWorldBorder(p, cfg), "clamp fires");
  Expect(p.x == 1000.0f, "clamped to soft extent");
  Expect(SoftBorderSpeedScale(glm::vec3(0, 0, 0), cfg) == 1.0f, "full speed center");
  Expect(SoftBorderSpeedScale(glm::vec3(1000, 0, 0), cfg) <= 0.26f,
         "edge speed slow");

  // --- Tickets / pools ---
  Expect(TicketLevelForRing(0) == ColumnTicketLevel::MeshFull, "ring0 full");
  Expect(TicketLevelForRing(2) == ColumnTicketLevel::MeshFull, "ring2 full");
  Expect(TicketLevelForRing(3) == ColumnTicketLevel::MeshLit, "ring3 lit");
  Expect(TicketLevelForRing(5) == ColumnTicketLevel::MeshDeferred, "ring5 deferred");
  WorkPoolBudget base = DefaultCruisePools();
  WorkPoolBudget hole = HoleDrainPools(base);
  Expect(hole.remesh_slots == 1, "hole drain keeps 1 remesh reservation");
  Expect(hole.first_mesh_slots == base.first_mesh_slots + base.remesh_slots - 1,
         "hole drain FM gets stolen remesh slots");

  // --- ColumnRecord store ---
  UColumnRecordStore store;
  store.SetEmerge(glm::ivec2(1, 2), ColumnEmergeState::Meshing);
  store.SetDesired(glm::ivec2(1, 2), ColumnDesiredStage::FirstMesh);
  const ColumnRecord *rec = store.Find(glm::ivec2(1, 2));
  Expect(rec != nullptr, "record exists");
  Expect(rec->emerge == ColumnEmergeState::Meshing, "emerge mirrored");
  Expect(rec->desired == ColumnDesiredStage::FirstMesh, "desired mirrored");
  store.Erase(glm::ivec2(1, 2));
  Expect(store.Find(glm::ivec2(1, 2)) == nullptr, "erase clears");

  ColumnRecord shadow_rec{};
  shadow_rec.mesh_rev = 3;
  shadow_rec.content_rev = 2;
  ColumnWorldTruth truth{};
  truth.has_chunk = true;
  truth.render_ready = true;
  UColumnRecordCoordinator::SyncFromWorldTruth(shadow_rec, truth);
  Expect(shadow_rec.published.gpu_handle == 0, "no synthetic gpu_handle=1");
  Expect(shadow_rec.published.shadow_synthetic, "render_ready shadow flagged");
  truth.published_gpu_handle = 42;
  UColumnRecordCoordinator::SyncFromWorldTruth(shadow_rec, truth);
  Expect(shadow_rec.published.gpu_handle == 42, "real gpu handle wired");
  Expect(!shadow_rec.published.shadow_synthetic, "real handle clears shadow");

  UColumnRecordCoordinator::ResetShadowMismatchCount();
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::ShadowCompare);
  Expect(UColumnRecordCoordinator::DecideFirstMeshEnqueue(true, false),
         "shadow: legacy owns enqueue");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 1,
         "shadow mismatch counted");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::FirstMeshOwner);
  Expect(!UColumnRecordCoordinator::DecideFirstMeshEnqueue(true, false),
         "FirstMeshOwner: record decides (no enqueue)");
  Expect(UColumnRecordCoordinator::DecideFirstMeshEnqueue(false, true),
         "FirstMeshOwner: record decides (enqueue)");
  // Rollback contract: flip stage → legacy.
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::ShadowCompare);
  Expect(UColumnRecordCoordinator::GetCutoverStage() ==
             ColumnCutoverStage::ShadowCompare,
         "rollback to ShadowCompare");

  UColumnRecordCoordinator::ResetShadowMismatchCount();
  Expect(UColumnRecordCoordinator::DecideRelightEnqueue(true, false),
         "shadow Relight: legacy owns");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 1,
         "Relight shadow mismatch counted");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::RelightOwner);
  Expect(!UColumnRecordCoordinator::DecideRelightEnqueue(true, false),
         "RelightOwner: record decides (no enqueue)");
  Expect(UColumnRecordCoordinator::DecideRelightEnqueue(false, true),
         "RelightOwner: record decides (enqueue)");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::ShadowCompare);

  UColumnRecordCoordinator::ResetShadowMismatchCount();
  Expect(UColumnRecordCoordinator::DecideSeamEnqueue(true, false),
         "shadow Seam: legacy owns");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 1,
         "Seam shadow mismatch counted");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::SeamOwner);
  Expect(!UColumnRecordCoordinator::DecideSeamEnqueue(true, false),
         "SeamOwner: record decides (no enqueue)");
  Expect(UColumnRecordCoordinator::DecideSeamEnqueue(false, true),
         "SeamOwner: record decides (enqueue)");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::ShadowCompare);

  UColumnRecordCoordinator::ResetShadowMismatchCount();
  Expect(UColumnRecordCoordinator::DecideEvict(true, false),
         "shadow Evict: legacy owns");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 1,
         "Evict shadow mismatch counted");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::EvictionOwner);
  Expect(!UColumnRecordCoordinator::DecideEvict(true, false),
         "EvictionOwner: record decides (no unload)");
  Expect(UColumnRecordCoordinator::DecideEvict(false, true),
         "EvictionOwner: record decides (unload)");
  UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage::ShadowCompare);

  // Q6 parity telem contract: cumulative counter is the SoT for
  // column_record_shadow_mismatch_n in perf JSONL (Decide* only).
  UColumnRecordCoordinator::ResetShadowMismatchCount();
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 0,
         "shadow counter reset for perf telem");
  (void)UColumnRecordCoordinator::DecideFirstMeshEnqueue(true, false);
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 1,
         "shadow mismatch feeds ColumnRecordShadowMismatchN");
  UColumnRecordCoordinator::ResetShadowMismatchCount();

  // Record wants derive from ColumnRecord SoT, not legacy job-stage map.
  ColumnRecord meshing_rec{};
  meshing_rec.resident = true;
  meshing_rec.pending.token = 1;
  meshing_rec.pending.stage = ColumnJobStage::Meshing;
  Expect(!UColumnRecordCoordinator::RecordWantsFirstMeshEnqueue(meshing_rec),
         "record rejects FirstMesh while Meshing pending");
  ColumnRecord gen_rec{};
  gen_rec.resident = true;
  Expect(UColumnRecordCoordinator::RecordWantsFirstMeshEnqueue(gen_rec),
         "record accepts FirstMesh for Gen column");
  Expect(UColumnRecordCoordinator::DecideFirstMeshEnqueue(true, false, {}, false)
             == true,
         "refresh-only mismatch does not block legacy enqueue");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 0,
         "count_mismatch=false skips Decide telem");

  // Stage shadow disagree is a per-pass gauge, not the Decide* counter.
  UColumnRecordCoordinator::SetShadowStageDisagreeFocusN(0);
  Expect(UColumnRecordCoordinator::ShadowStageDisagreeFocusN() == 0,
         "stage disagree gauge reset");
  UColumnRecordCoordinator::SetShadowStageDisagreeFocusN(42);
  Expect(UColumnRecordCoordinator::ShadowStageDisagreeFocusN() == 42,
         "stage disagree gauge set");
  Expect(UColumnRecordCoordinator::ShadowMismatchCount() == 0,
         "stage gauge does not touch Decide mismatch");
  UColumnRecordCoordinator::SetShadowStageDisagreeFocusN(0);

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return 1;
  }
  std::cout << "CruiseSoTPhaseTest OK\n";
  return 0;
}
