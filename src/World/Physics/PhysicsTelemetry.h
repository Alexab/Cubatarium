#ifndef PHYSICSTELEMETRY_H
#define PHYSICSTELEMETRY_H

#include <cstdint>
#include <string>

namespace cutum
{

struct PhysicsTelemetry
{
  double PhysicsStepMs{0.0};
  double MovementStepMs{0.0};
  double StreamMs{0.0};
  double MeshEmergeMs{0.0};
  double BlockStepMs{0.0};
  double DrainStepMs{0.0};
  double FluidStepMs{0.0};
  int SimulationStepsThisFrame{0};
  int PhysicsSubsteps{0};
  double PhysicsAccumMs{0.0};
  uint64_t BlockQueueDepth{0};
  uint64_t LiquidQueueDepth{0};
  uint64_t CollisionRebuildBacklog{0};
  uint64_t VisualRemeshBacklog{0};
  uint64_t DeferredUpdates{0};
  uint64_t DroppedUpdates{0};
  uint64_t PurgedUpdates{0};
  uint64_t CollisionBroadphaseRejects{0};
  uint64_t CollisionBroadphaseFallbacks{0};
  uint64_t CollisionReadyTransitions{0};
  double CollisionReadyWaitMs{0.0};
  double FastRelightMs{0.0};
  double FullRelightMs{0.0};
  double EditToFirstMeshMs{0.0};
  uint64_t PendingPlayerRelights{0};
  uint64_t PendingBackgroundRelights{0};
  uint64_t AsyncRelightInflight{0};
  uint64_t RelightDiscardedLate{0};
  uint64_t MeshDiscardedLate{0};
  /// ApplyMeshResult rejected as stale (revision mismatch) — remesh thrash signal.
  uint64_t MeshApplyStale{0};
  /// Era15 TD-049: CPU replace published before FreeChunk when GPU-only drawable.
  uint64_t MeshReplaceHoleAvoided{0};
  /// Deferred GPU mesh applies waiting for ProcessPendingGpuMeshes.
  int PendingGpuAppliesN{0};
  /// Queued phase only (not yet Kick).
  int PendingGpuQueuedN{0};
  /// Kicked phase (fence outstanding); capped by readback ring.
  int PendingGpuKickedN{0};
  /// Final mesh schedule/drain after TickMeshEmerge (MeshWorkAdmission SoT).
  int MeshScheduleFinal{0};
  int MeshDrainFinal{0};
  int MeshAdmissionMode{0};
  /// Kicks issued in last RebuildDirtyChunksWithStats tick.
  int GpuKickN{0};
  /// Successful Finish+Commit in last rebuild tick.
  int GpuFinishN{0};
  /// NotReady Finish polls in last rebuild tick.
  int GpuFinishNotReadyN{0};
  /// Ground columns in render ring without greedy mesh (excl. GpuExtractInFlight).
  int PostLoadRingNotReady{0};
  /// Missing greedy count when exiting EnterGame GPU warmup (diag snapshot).
  int EnterGameWarmupMissingGreedy{0};
  /// Frames where SoftDefer FOV+pending applied a Capture budget floor (cumulative).
  uint64_t SoftDeferCaptureFloorHits{0};
  /// SoftDefer/rim FirstMesh ticket retargeted from focus to MissCx/Cz (cumulative).
  uint64_t SoftDeferWitnessRetarget{0};
  /// Last witness MissHoriz when SoftDeferWitnessRetarget fired (0 if focus).
  int SoftDeferWitnessHoriz{0};
  /// Capture/relight bg budget requested by SoftDefer floor this frame (0 if idle).
  int SoftDeferCaptureBudget{0};
  /// Era19 FrameStreamingBudget soft ms SoT (bad-frame threshold).
  int FrameBudgetMs{0};
  /// 1 when Capture floor would spend on an already-hot frame.
  int CaptureOverBudget{0};
  /// 1 when VB Capture/bg heal was deferred for miss/hitch (I-B1/I-B2).
  int HealDeferredForMiss{0};
  /// Era19 P2: Remesh skipped because column is in PendingLight stage.
  uint64_t StageSkipRemeshPendingLight{0};
  /// Empty SoftDefer placeholders seen by undrawn heal this frame (A2 smoke).
  int SoftDeferEmptyPlaceholderN{0};
  /// Era38: SoftDefer empty coords with horiz<=2 (previous frame until scan).
  int SoftDeferEmptyNearN{0};
  /// Stuck pattern: HasGreedy && !Drawable && !Dirty && horiz>1.
  int SoftDeferEmptyStuckN{0};
  int SoftDeferEmptyStuckCx{0};
  int SoftDeferEmptyStuckCy{0};
  int SoftDeferEmptyStuckCz{0};
  int SoftDeferEmptyStuckHoriz{0};
  int SoftDeferEmptyStuckDefer{0};
  /// Era24: max SoftDefer empty age (frames) seen this tick.
  int SoftDeferEmptyAgeMaxFrames{0};
  /// Era24: SoftDefer empty coords with ColumnFlow FirstMesh Contains.
  int SoftDeferEmptyOwnedN{0};
  /// Era24: SoftDefer empty publish avoided (Hide⇒Ticket), cumulative.
  uint64_t SoftDeferEmptyPublishAvoided{0};
  /// SoftDeferHeld side-set size (outside-focus !Drawable FirstMesh).
  int SoftDeferHeldN{0};
  double RelightCompletedPerSec{0.0};
  double CommitPhysicsMs{0.0};
  double CommitRelightMs{0.0};
  double CommitMeshMs{0.0};
  double CommitApplyMs{0.0};
  double CommitSealMs{0.0};
  /// Breakdown inside StreamMs (UpdateStreaming + TickAsyncChunkSystems).
  double StreamerUpdateMs{0.0};
  double AsyncIoMs{0.0};
  double RelightDrainMs{0.0};
  /// P2: Capture (DrainRelightQueues) vs Apply (DrainAsyncRelightResults).
  double RelightCaptureMs{0.0};
  double RelightApplyMs{0.0};
  /// Previous frame (P2 apply budget / P5 dynamic bg_cap).
  double RelightDrainMsPrev{0.0};
  double RelightApplyMsPrev{0.0};
  /// Previous-frame Apply count (unit-cost estimator for cruise Apply budget).
  int RelightApplyNPrev{0};
  double MeshSyncMs{0.0};
  double MeshSnapshotMs{0.0};
  /// Wall time spent in RebuildChunkImmediate this frame (inside MeshEmergeMs).
  double MeshImmediateMs{0.0};
  int MeshImmediateCount{0};
  /// WindowManager::Update split (outside PhysicsStepMs).
  double ViewsMs{0.0};
  double DoMovementMs{0.0};
  double EnsureCollisionMs{0.0};
  double CreatureTickMs{0.0};
  double CameraDoMovementMs{0.0};
  double CameraGroundSupportMs{0.0};
  double CameraLocomotionMs{0.0};
  double CameraHorizMoveMs{0.0};
  double CameraSyncMs{0.0};
  /// Era14: TickWorldStreamingPhase wall (stream+emerge); outside DoMovementMs.
  double WorldStreamingPhaseMs{0.0};
  double BlockInputMs{0.0};
  /// TickEnvironment wall inside DoMovement (before PhysicsStep timer).
  double TickEnvMs{0.0};
  /// Break UX diagnostics (per-frame event counts; reset each Update).
  int BreakCompleteN{0};
  int BreakInflightRaceN{0};
  int BreakDarkFaceN{0};
  /// Place UX diagnostics (per-frame; reset each Update).
  int PlaceCompleteN{0};
  int PlaceEmissionN{0};
  /// Autosave deferred Begin / skipped Tick while edit-hot (per-frame).
  int AutosaveDeferredN{0};
  int AutosaveSkippedTickN{0};
  /// DigSeam: P2-demoted face remesh queue (per-frame after drain).
  int DigSeamPendingN{0};
  int DigSeamRemeshN{0};
  /// Stand rim heal (manual 131827): stale-wave cols / Dirty / calm Imm.
  int StaleRepairWaveN{0};
  int StandRimDirtyN{0};
  int StandRimImmN{0};
  /// Max light emission among blocks edited this frame (0 if none).
  int EditLightEmission{0};
  /// RebuildDirtyChunksWithStats wall (sync fill + schedule + apply drain).
  double MeshDirtyTickMs{0.0};
  /// Cruise wall A1: mesh_dirty_tick substages.
  double MeshDirtyPruneMs{0.0};
  int MeshDirtyPruneN{0};
  double MeshDirtySortMs{0.0};
  double MeshDirtyDrainMs{0.0};
  int MeshDirtyDrainN{0};
  double MeshDirtyScheduleMs{0.0};
  int MeshDirtyScheduleOkN{0};
  int MeshDirtyScheduleSkipN{0};
  double MeshDirtyGpuMs{0.0};
  int MeshDirtyGpuN{0};
  double MeshDirtySyncMs{0.0};
  int MeshDirtySyncN{0};
  int DirtyTouchN{0};
  int DirtyRevisitSameN{0};
  int DirtyFmN{0};
  int DirtyRemeshN{0};
  /// Real CountUnfinishedVisualNear invocations this frame (not prep hooks).
  int PrepUnfinishedCallsN{0};
  /// Cruise wall P3: full O(R²) unfinished rescans vs incremental hits.
  int PrepUnfinishedFullN{0};
  int PrepUnfinishedIncrementalN{0};
  /// UnfinishedVisualCache: pure hit (no dirty recheck) vs NoteDirty overflow.
  int UnfinishedCacheHitN{0};
  int UnfinishedCacheOverflowN{0};
  /// MeshWorkAdmission caps after Finalize (pool util SoT).
  int DirtyAdmitBudgetEnd{0};
  int FirstMeshScheduleCap{0};
  int RemeshScheduleCap{0};
  /// Per-frame TrimFarRelightFifo drops (not cumulative RelightFifoDropped).
  int RelightTrimFarN{0};
  /// P1: per-frame FIFO drops (overflow + trim) and pin keys skipped.
  int RelightFifoDropN{0};
  int RelightFifoPinSavedN{0};
  int RelightFifoDropNPrev{0};
  int RelightFifoPinDropNPrev{0};
  /// Player block/world position for cruise SLA (perf jsonl).
  float PlayerX{0.0f};
  float PlayerY{0.0f};
  float PlayerZ{0.0f};
  /// Era14.1 phase budget: stream already over budget; miss carve-out active.
  int PhaseBudgetOver{0};
  int PhaseMissCarveOut{0};
  /// Cruise wall P2: miss reserved / emerge cap from phase time-slice (ms).
  double MissReservedMs{0.0};
  double EmergeBudgetCapMs{0.0};
  /// TickMeshEmerge wall before RebuildDirtyChunksWithStats (prep/idle/cold).
  double MeshEmergePrepMs{0.0};
  /// I5 prep sub-timers (ms) inside MeshEmergePrepMs.
  double MeshEmergePrepMissingMs{0.0};
  /// Legacy bucket name: SoftDefer setup + pending/dirty/black scans in emerge.
  /// Prefer PrepPendingLightMs / PrepBlackStickyMs / PrepDirtyCountMs.
  double MeshEmergePrepUnfinishedMs{0.0};
  double MeshEmergePrepStickyMs{0.0};
  double MeshEmergePrepDropDirtyMs{0.0};
  double MeshEmergePrepOtherMs{0.0};
  /// Closeout Phase A: split of former MeshEmergePrepUnfinishedMs.
  double PrepPendingLightMs{0.0};
  double PrepBlackStickyMs{0.0};
  double PrepDirtyCountMs{0.0};
  double PrepSoftdeferSetupMs{0.0};
  /// SoftDefer empty candidate collect (after prep_softdefer_setup).
  double SoftdeferEmptyScanMs{0.0};
  /// SoftDefer empty seam remesh + ownership apply.
  double SoftdeferEmptyOwnMs{0.0};
  int PrefetchVisualOps{0};
  int PrefetchKeepOps{0};
  int GenBacklogTotal{0};
  int KeepCols{0};
  int VisualCols{0};
  double IdlePrefetchMs{0.0};
  /// Streaming gate diagnostics (filled each UpdateStreaming).
  int StreamLoads{0};
  int StreamAsyncQueued{0};
  /// Era25 I-F1: disk Ensure complete this frame (honest vs stream_loads).
  int StreamDiskCompleteN{0};
  /// Era25 I-F1: gen scheduler commits this frame.
  int StreamGenCommitN{0};
  /// Era25 I-F4: frontier_pressure predicate result (0/1).
  int FrontierPressure{0};
  int StreamRingBlocked{0};
  int StreamNearSkipped{0};
  int StreamLoadCandidates{0};
  /// Era51: procedural fill enabled (saved world + streaming).
  int AllowProcFill{0};
  /// Era51: columns in RD with terrain absent from BlockWorld (load backlog).
  int ColumnAbsentInRdN{0};
  /// Era51: columns in RD loaded but not render-ready (mesh/light debt).
  int ColumnLoadedNoMeshN{0};
  /// Exclusive ColumnEmergeState bump denials this frame (competing producer).
  int ColumnBumpDenied{0};
  /// ColPipe P1: ColumnFlow ExclusiveRank upgrades this frame.
  int ColumnFlowUpgradeN{0};
  /// Live ColumnEmergeState counts (Lighting / Meshing / RenderReady).
  int ColumnLightingN{0};
  int ColumnMeshingN{0};
  int ColumnRenderReadyN{0};
  int PendingLightCount{0};
  int FocusChunkX{0};
  int FocusChunkZ{0};
  int UnderfeetNeed{0};
  /// Underfeet column SoT vs draw (invisible-ready blind spot).
  int UnderfeetDrawOk{0};
  int UnderfeetHasMesh{0};
  int UnderfeetSticky{0};
  int UnderfeetPendingLight{0};
  /// ColumnRenderableState::BlockReason as int.
  int UnderfeetReason{0};
  /// ColumnEmergeState as int for underfeet column.
  int UnderfeetStage{0};
  /// 1 if world LightingRelightDeferred is enabled this frame.
  int LightingRelightDeferred{0};
  /// 1 when underfeet xz appears in filtered opaque draw refs this frame.
  int UnderfeetOpaquePresent{0};
  /// FlickerZero V3: post-draw latch for streaming/perf SoT (prior frame).
  int UnderfeetOpaquePresentLatched{0};
  /// FZ2-R3: reconcile SoT for perf when draw_ok (streaming sample).
  int UnderfeetOpaquePresentPredicted{0};
  /// FZ2-R3: raw post-draw opaque pass witness (debug).
  int UnderfeetOpaquePresentRaw{0};
  /// FogPullIn effective state (0 = disabled / unset).
  int FogPullInRd{0};
  int FogPullInMargin{0};
  float FogPullInStartRatio{0.0f};
  int FogHoleDebt{0};
  /// Missing mesh in focus (alias of VisualHoles). Dark/light debt are separate.
  int NearFocusHoles{0};
  /// Missing GreedyCache in focus (visual holes only).
  int VisualHoles{0};
  /// Missing mesh or dark/unlit preview in focus (render contract).
  int UnfinishedVisual{0};
  /// PendingLightBeforeMesh in focus (light debt, not visual holes).
  int LightDebt{0};
  /// Count of focus columns with missing mesh (0..N).
  int FocusMissingMesh{0};
  /// Nearest missing slice witness (valid only when FocusMissingMesh!=0).
  int MissCx{0};
  int MissCy{0};
  int MissCz{0};
  int MissHoriz{0};
  /// Count of focus columns with mesh but no sky light sample.
  int FocusDarkMesh{0};
  /// Era39 A4: pending-dark (hidden until lit) vs sticky remesh (preview class).
  int FocusDarkMeshHidden{0};
  int FocusDarkMeshPreview{0};
  /// Pending-light + sticky black preview columns in focus (subset of dark).
  int FocusPendingDark{0};
  int FocusStickyRemesh{0};
  /// Era16 TD-052: focus columns with drawable stale/fully-dark mesh (user black).
  int VisibleBlackFocusN{0};
  /// Subset of VisibleBlackFocusN without Flow Contains / Progress / Sticky.
  int VisibleBlackNoTicketN{0};
  /// Era17: VB with Contains ∨ Dirty/Inflight/PendingLight ∨ Sticky.
  int VisibleBlackProgressN{0};
  /// Era17: VB with Contains but no Dirty/Inflight/PendingLight (queued stall).
  int VisibleBlackStalledN{0};
  /// Focus columns failing SoT unfinished visual (alias of UnfinishedVisual sample).
  /// Not pending+dirty pressure — see FocusPressure.
  int FocusNotRenderReady{0};
  /// Pending+dirty scheduler pressure (SoftDefer Capture / ingress only).
  int FocusPressure{0};
  /// Dirty mesh chunks inside focus radius (lit-but-dirty remesh debt).
  int FocusDirtyChunks{0};
  /// Unfinished focus columns ahead of movement/view forward (dot >= 0).
  int FocusUnfinishedAhead{0};
  /// Unfinished focus columns behind movement/view forward (dot < 0).
  int FocusUnfinishedBehind{0};
  /// 0=Green, 1=Yellow, 2=Red (StreamingPressureLevel).
  int StreamPressure{0};
  /// P3 soft flight integrity: multiply horizontal cruise (1 = off; ~0.55–0.7).
  float StreamSpeedClampScale{1.0f};
  /// PendingLightBeforeMesh count inside focus radius (vs global PendingLightCount).
  int PendingLightFocus{0};
  /// Comma-separated (cx,cz) for focus pending columns (telemetry only).
  std::string PendingFocusCols;
  /// Memory budget fill / pressure (Era 12).
  int MeshCompletedN{0};
  int MeshCompletedCap{0};
  uint64_t MeshCompletedDiscarded{0};
  int RelightCompletedN{0};
  int RelightCompletedCap{0};
  uint64_t RelightCompletedDiscarded{0};
  /// P0: last DrainRelightQueues Capture (horiz of drained column, 0/1 finalize,
  /// Y-span in chunk layers). RelightApplyN = DrainCompleted count this frame
  /// (throughput; RelightCompletedN is ring occupancy at sample).
  int RelightCaptureColHoriz{-1};
  int RelightCaptureFinalize{0};
  int RelightCaptureBandCySpan{0};
  int RelightCaptureFullN{0};
  int RelightCaptureNeighborLightN{0};
  int RelightWitnessHoldN{0};
  int RelightApplyN{0};
  /// Apply with finalize_pending_gate=false (partial Y-band; pending kept).
  int RelightApplyPartialN{0};
  /// Apply with finalize_pending_gate=true (pending gate may clear).
  int RelightApplyFinalN{0};
  int DirtyN{0};
  int PendingLightN{0};
  /// Era37 P1c: underground relight skip / false-clear counters.
  uint64_t RelightSkippedUndergroundN{0};
  uint64_t RelightFalseClearN{0};
  uint64_t RelightSkippedNoOpEnqueueN{0};
  uint64_t RelightDeferredFarEnqueueN{0};
  int RelightDeferredFarPendingN{0};
  /// FZ2.2-O1: NotePendingLight skipped (inflight or already pending).
  uint64_t RelightNoteSkippedDupN{0};
  /// FZ2.3-O2: Capture finalize skipped (same-column epoch dedup).
  uint64_t RelightFinalizeDedupN{0};
  /// Cruise Capture SoT: effective bg cap / Y-band cy this DrainRelightQueues.
  int CaptureBgCapN{0};
  int CaptureBandCy{0};
  /// Disk lightmap trust (light_complete).
  int DiskLightTrustedN{0};
  int DiskLightRepairedN{0};
  int RelightFifoN{0};
  /// Era45: MarkRelit RequestRemeshAfterApply calls (cumulative).
  uint64_t MarkRelitRemeshAfterApplyN{0};
  /// Era47 P0: MarkRelit PreferKickGpu decisions (cumulative).
  uint64_t MarkRelitPreferKickN{0};
  /// Era22 sticky-settle: per-frame MarkRelit ClassifyRemeshAfterLitApply decisions.
  int MarkRelitSkipAlreadyDirtyN{0};
  int MarkRelitSkipAlreadyRaaN{0};
  int MarkRelitSkipInflightN{0};
  int MarkRelitSkipEnterLitQuiesceN{0};
  int MarkRelitScheduleN{0};
  /// Schedule path suppressed by enter-settled gate (still classified Schedule).
  int MarkRelitSuppressEnterSettledN{0};
  /// StickyRemeshAfterLight insert/erase reasons (per frame).
  int StickyInsertStaleAfterApplyN{0};
  int StickyInsertSeamN{0};
  int StickyInsertOtherN{0};
  int StickyEraseDrawableN{0};
  int StickyErasePendingClearN{0};
  int StickyErasePrunedFarN{0};
  int StickyEraseRemeshCommitN{0};
  int StickyEraseOtherN{0};
  /// Era46: RAA commit scheduled MarkDirty (not PreferKick).
  uint64_t RaaCommitMarkDirtyN{0};
  /// Era46: MarkDirty mid-flight deferred into RemeshAfterApply.
  uint64_t MarkDirtyToRaaN{0};
  /// Era47 P0: Dirty schedule skipped because async InFlight (cumulative).
  uint64_t DirtyScheduleSkipInflightN{0};
  uint64_t DirtyDropped{0};
  uint64_t PendingLightDropped{0};
  uint64_t RelightFifoDropped{0};
  /// P1: pin-key drops this frame (gate: stay 0).
  int RelightFifoPinDropN{0};
  double GpuPoolUsedMb{0.0};
  double GpuPoolCapMb{0.0};
  /// Init-bound backend names (mesher/store/cull).
  std::string BackendMesher{"cpu_greedy"};
  std::string BackendStore{"cpu_staging"};
  std::string BackendCull{"cpu_frustum"};
  uint64_t GpuDrawCmds{0};
  double GpuCullMs{0.0};
  /// CPU wall around GPU compact dispatch (excludes SubData); 0 on CPU cull.
  double GpuCullGpuMs{0.0};
  double VertexPoolFill{0.0};
  /// 1 when opaque cull used GPU compact→indirect (no flat-ref rebuild).
  double GpuCullIndirect{0.0};
  /// Visual-debug: opaque MDI cmds after compact cull.
  uint64_t OpaqueCmdTotal{0};
  uint64_t OpaqueCmdOn{0};
  /// GpuPacked opaque refs drawn this frame (separate from MDI).
  uint64_t OpaqueGpuPackedN{0};
  /// Draw SoT: OpaqueCmdOn + OpaqueGpuPackedN (honest opaque presence).
  uint64_t OpaqueDrawN{0};
  /// Stage sizes before compact cull (diag for opaque collapse).
  uint64_t OpaqueRefsCpuVis{0};
  uint64_t OpaqueRefsRenderReady{0};
  uint64_t OpaqueMdiEligible{0};
  uint64_t CrossBatchCount{0};
  uint64_t CpuAabbWouldOn{0};
  uint64_t EditImmediateN{0};
  uint64_t EditDirtyN{0};
  uint64_t EditNeighborPendingFrames{0};
  uint64_t PoolUnsyncUploads{0};
  double PoolFenceWaitMs{0.0};
  /// Focus column split: meshed-but-culled vs not ready / unlit preview.
  uint64_t ChunkMeshedCulled0{0};
  uint64_t ChunkMeshedUnlit{0};
  /// Era39 A4: Type A split — hidden (pending-dark) vs sticky preview.
  uint64_t ChunkMeshedUnlitHidden{0};
  uint64_t ChunkMeshedUnlitPreview{0};
  uint64_t ChunkNotReady{0};
  /// Nearest non-bottom greedy vertex with sky+block light==0 near camera (diag).
  int DarkFaceNearN{0};
  /// Subset of DarkFaceNearN: mesh dark but light-field currently lit (repairable).
  int DarkFaceStaleNearN{0};
  /// Subset: mesh dark and light-field also 0 (world edge / cave / unloaded).
  int DarkFaceVoidNearN{0};
  int DarkFaceBlockX{0};
  int DarkFaceBlockY{0};
  int DarkFaceBlockZ{0};
  int DarkFaceChunkX{0};
  int DarkFaceChunkY{0};
  int DarkFaceChunkZ{0};
  int DarkFaceBlockId{0};
  int DarkFaceIndex{0};
  double DarkFaceDist{0.0};
  uint64_t GpuMeshVboDispatch{0};
  uint64_t GpuLightSeedApply{0};
  /// 1 when PreferGpu fluid column scan is active this frame.
  double GpuFluidScanOn{0.0};
  std::string BackendFluid{"cpu_fluid_surface"};
  /// "flat" | "full" | "gpu_full" — active lighting pipeline mode.
  std::string BackendLightingMode{"full"};
  uint64_t GpuMaskReadback{0};
  uint64_t GpuBlocklightFlood{0};
  int MemoryPressure{0};
  int KeepMarginEff{0};
  uint64_t BufferExpandEvents{0};
  /// GPF6 Android GPU policy telemetry (0/1 doubles for analyze medians).
  double CapsHasCompute{0.0};
  double CapsHasSsbo{0.0};
  double CapsProbeCompleted{0.0};
  double AndroidGpuUserPref{1.0};
  double AndroidGpuEffective{0.0};
  std::string AndroidGpuDenyReason{"n/a"};
  std::string GlVersion;
  std::string GlRenderer;
  /// CreatureTick split (appended so existing offsets stay stable).
  double EnvironmentTickMs{0.0};
  double NpcIntentExecuteMs{0.0};
  double ControlledInfluenceMs{0.0};
  double VitalsTickMs{0.0};
  double StatusEffectsTickMs{0.0};
  int CreaturesTotal{0};
  int CreaturesAiTicked{0};
  int WorldCreaturesSkipped{0};
  /// Input-first A1: player locomotion before world AI tail.
  double PlayerLocomotionBlockMs{0.0};
  double WorldAiAfterPlayerMs{0.0};
  int CreaturesAiBudget{0};
  int CreaturesAiDeferred{0};
};

} // namespace cutum

#endif // PHYSICSTELEMETRY_H
