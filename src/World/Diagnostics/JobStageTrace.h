#ifndef JOB_STAGE_TRACE_H
#define JOB_STAGE_TRACE_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace cutum
{

/// A21 P0.3: compact per-chunk job stage span for emergency dump / JSONL.
/// Does not log the full world each frame — fixed ring buffer only.
enum class JobStage : uint8_t
{
  Created = 0,
  Admitted,
  Started,
  Built,
  Uploaded,
  Published,
  Retired,
  Cancelled,
  GpuQueued,
  GpuKicked,
  GpuCountersReady,
  GpuReady,
  Count
};

/// Why a built mesh result stopped progressing through its job lifecycle.
/// Kept separately from outcome, which describes install/render-demand state.
enum class JobTerminalReason : uint8_t
{
  None = 0,
  StaleInputStamp,
  StaleCatalog,
  StaleGeometryInput,
  StaleLightInput,
  ChunkNotResident,
  NoActiveOwner,
  SupersededByNewerRevision,
  CurrentRevisionAdvanced,
  DarkMeshRejected,
  GpuAdmissionDeferred,
  PublicationRejected,
  SoftDeferPriorGpuRetained,
  SoftDeferPriorMeshRetained,
  SoftDeferRetryRequired,
  SoftDeferFirstMeshHeld,
  ResultMemoryBudgetRejected,
  CompletedQueueOverflow,
  CompletedQueueCapacityReduced,
  SubmissionEpochChanged,
  JobIdentityReplaced,
  WorkerPoolRejected,
  GpuPipelineFailed
};

struct JobStageSpan
{
  int32_t cx{0};
  int32_t cy{0};
  int32_t cz{0};
  /// Internal async mesh job identity, distinct from render-demand attempt ID.
  uint64_t job_id{0};
  uint64_t incarnation{0};
  uint64_t attempt_id{0};
  /// Typed render stamps; legacy desired_rev/source_rev/published_rev remain
  /// for existing consumers and may belong to different revision domains.
  uint64_t desired_geom_rev{0};
  uint64_t source_geom_rev{0};
  uint64_t published_geom_rev{0};
  uint64_t source_light_rev{0};
  uint64_t desired_rev{0};
  uint64_t source_rev{0};
  uint64_t published_rev{0};
  uint64_t world_epoch{0};
  uint64_t desired_light_rev{0};
  uint64_t published_light_rev{0};
  uint64_t desired_coverage_gen{0};
  uint64_t published_coverage_gen{0};
  uint8_t face_mask{0};
  uint8_t outcome{0};
  uint8_t cull_decision{0};
  JobStage stage{JobStage::Created};
  JobTerminalReason terminal_reason{JobTerminalReason::None};
  uint8_t queue_reason{0};
  double created_ms{0.0};
  double stage_ms{0.0};
  /// Milliseconds from mesh-job creation to this event, when available.
  double elapsed_ms{0.0};
};

/// State transitions for the per-slice render-demand owner. JobStageSpan is
/// worker/GPU centric; this trace records the target revision and attempt
/// lifetime that those jobs are meant to satisfy.
enum class DemandTransitionKind : uint8_t
{
  AttemptCreated = 0,
  TargetAdvanced,
  StageAdvanced,
  InstallPublished,
  InstallRetained,
  InstallRejected,
  InstallCancelled,
  StaleInstallIgnored,
  PublishedRevisionAdvanced,
  AttemptReminted,
  MeshRevisionBumped,
  IdentityReset
};

enum class MeshRevisionBumpReason : uint8_t
{
  Unknown = 0,
  MarkDirtyEnqueued,
  PriorityEnterSoftDefer,
  PriorityEnterFirstMesh,
  PriorityFullyDarkRemesh,
  PriorityDirtyEnqueued,
  InvalidatedInFlight,
  MissingHoleSeamNeighbor,
  MarkRelitInstall,
  FaceDebtCallback,
  SoftDeferVisibilitySeam,
  SettledDrawGateRepair,
  VisualRebuildQueue,
  FluidWorldGeometry,
  TerrainColumnInvalidation,
  TerrainChunkEmergence,
  BlockRegistryInvalidation,
  PostLightMeshFinalize,
  WorldStreamingColumnCommit,
  WorldStreamingLitFinalize,
  RelitOrphanGround,
  FirstDrawableSeaSeam,
  SurfaceDarkRepairSeam,
  PersistenceTerrainLoad,
  StreamerCommitSeaSeam,
  PendingLightColumnRecovery,
  PriorityWorldStreamingRepair,
  PriorityChunkEmergeRepair,
  PriorityWorldCoreRepair,
  PriorityRelitInstallRepair,
  PriorityWorldStreamingCommit,
  PriorityWorldCoreCommit
};

struct DemandTransitionSpan
{
  int32_t cx{0};
  int32_t cy{0};
  int32_t cz{0};
  uint64_t world_epoch{0};
  uint64_t incarnation{0};
  uint64_t previous_attempt_id{0};
  uint64_t attempt_id{0};
  uint64_t previous_desired_geom_rev{0};
  uint64_t desired_geom_rev{0};
  uint64_t previous_desired_light_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t previous_desired_coverage_gen{0};
  uint64_t desired_coverage_gen{0};
  uint64_t previous_published_geom_rev{0};
  uint64_t published_geom_rev{0};
  uint64_t previous_published_light_rev{0};
  uint64_t published_light_rev{0};
  uint64_t previous_published_coverage_gen{0};
  uint64_t published_coverage_gen{0};
  uint64_t mesh_revision_before{0};
  uint64_t mesh_revision_after{0};
  uint32_t mesh_owner_flags{0};
  JobStage previous_stage{JobStage::Created};
  JobStage stage{JobStage::Created};
  DemandTransitionKind kind{DemandTransitionKind::AttemptCreated};
  MeshRevisionBumpReason mesh_revision_bump_reason{
      MeshRevisionBumpReason::Unknown};
  uint8_t result{0};
  uint8_t had_active_attempt{0};
  uint8_t has_active_attempt{0};
  uint8_t retained_awaiting_successor{0};
  double event_ms{0.0};
};

/// Bounded, opt-in sample of a chunk counted by the visible-black census.
/// Captures both column-level ownership flags and the exact slice's mesh/demand
/// stamps so aggregate census counts can be traced to their real work owner.
struct VisualBlackTraceRecord
{
  /// 0=black-attribution, 1=focus slice, 2=renderer draw-gate candidate,
  /// 3=visible relight-queue admission, 4=ordinary focus remesh attempt,
  /// 5=draw-gate relight scan counts, 6=near-focus FirstMesh attempt,
  /// 7=priority-remesh scheduling attempt, 8=frustum candidate, 9=screen pixel
  /// sampled after transparent pass (normal RGBA or diagnostic marker), joined
  /// to lifecycle/source-mesh state of the exact ray-mapped chunk slice.
  uint8_t sample_kind{0};
  uint8_t focus_state{0};
  int32_t cx{0};
  int32_t cy{0};
  int32_t cz{0};
  int32_t focus_cx{0};
  int32_t focus_cz{0};
  int32_t camera_x{0};
  int32_t camera_y{0};
  int32_t camera_z{0};
  int32_t non_air_blocks{0};
  uint64_t frame_epoch{0};
  uint64_t world_epoch{0};
  /// Incarnation of the resident voxel chunk (never overwritten by demand).
  uint64_t incarnation{0};
  /// Incarnation recorded by the demand store; diagnose stale coordinate reuse.
  uint64_t demand_incarnation{0};
  uint64_t chunk_content_revision{0};
  uint64_t mesh_revision{0};
  uint64_t attempt_id{0};
  uint64_t desired_geom_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t desired_coverage_gen{0};
  uint64_t demand_published_geom_rev{0};
  uint64_t demand_published_light_rev{0};
  uint64_t demand_published_coverage_gen{0};
  uint64_t settled_light_rev{0};
  uint8_t has_settled_light{0};
  double demand_attempt_age_ms{0.0};
  double demand_progress_age_ms{0.0};
  uint64_t published_geom_rev{0};
  uint64_t published_light_rev{0};
  uint64_t meshed_light_rev{0};
  uint64_t field_light_rev{0};
  int32_t stale_sample_x{0};
  int32_t stale_sample_y{0};
  int32_t stale_sample_z{0};
  int32_t stale_source_cx{0};
  int32_t stale_source_cy{0};
  int32_t stale_source_cz{0};
  uint64_t stale_source_incarnation{0};
  uint64_t stale_source_light_rev{0};
  /// VisibleBlackCause ordinal from VisibleBlackAttribution.h.
  uint8_t cause{0};
  uint8_t active_stage{0};
  uint8_t face_debt_mask{0};
  uint8_t overlay_face_debt_mask{0};
  uint8_t peer_face_debt_mask{0};
  /// sample_kind=1: per-face debt requirements and six-face neighbor census.
  /// Face order matches ChunkMeshSnapshot: -X, +X, -Y, +Y, -Z, +Z.
  uint64_t face_waiting_peer_gen[6]{};
  uint64_t face_peer_effective_gen[6]{};
  uint64_t face_peer_published_geom_rev[6]{};
  uint64_t face_peer_demand_published_geom_rev[6]{};
  uint64_t face_peer_published_coverage_gen[6]{};
  uint64_t face_peer_desired_coverage_gen[6]{};
  uint64_t face_peer_incarnation[6]{};
  uint8_t face_peer_loaded_mask{0};
  uint8_t face_peer_nonair_mask{0};
  uint8_t face_peer_drawable_mask{0};
  uint8_t face_peer_satisfying_mask{0};
  uint16_t face_focus_boundary_non_air[6]{};
  uint16_t face_peer_boundary_non_air[6]{};
  /// sample_kind 4/6/7: SnapshotAcquireDeferReason, set when cause=13.
  uint8_t mesh_snapshot_defer_reason{0};
  /// sample_kind 4/6/7: MeshEnqueueResult, set when cause=14.
  uint8_t mesh_enqueue_reject_reason{0};
  uint8_t draw_gate_ready{0};
  uint8_t stale_face_index{0};
  uint8_t stale_sample_light{0};
  uint8_t stale_sample_gpu_path{0};
  /// sample_kind=2: 1=CPU opaque, 2=CPU transparent, 3=packed opaque,
  /// 4=packed transparent.
  uint8_t renderer_path{0};
  uint32_t renderer_cpu_index_count{0};
  uint32_t renderer_gpu_quad_count{0};
  /// sample_kind=2 flags 0..17; sample_kind=9 uses the same flags for the
  /// exact ray-mapped pixel chunk: drawable, satisfying, live GPU, fully dark,
  /// lit drawable, stale dark, dirty, mesh in-flight, GPU pending, extract
  /// in-flight, queued/kicked GPU apply, pending light, async relight, sticky
  /// remesh, repair progress, and column draw-ready/repair-ticket.
  /// Bits 18..21: settlement exists, matches current field revision, demand
  /// light is current, and the exact dark drawable satisfies its draw gate.
  /// Bit 22: this drawable is temporarily shown with the ambient preview floor.
  uint32_t renderer_gate_flags{0};
  /// sample_kind=8 bits 0..5: drawable, satisfying, live GPU draw, prepared
  /// CPU renderer ref, passed the render-ready gate, and prepared GPU-packed
  /// ref. `focus_state`: 1=no drawable, 2=drawable missing from CPU/packed
  /// snapshot, 3=CPU/packed ref rejected by render-ready gate, 4=ready CPU/
  /// packed ref sampled to inspect its actual post-cull GPU command.
  /// GPU MDI ownership is reported separately below; absence from CPU/packed
  /// refs does not imply absence from an MDI pass.
  uint8_t renderer_column_reason{0};
  uint8_t renderer_column_draw_ok{0};
  uint8_t renderer_column_has_repair_ticket{0};
  /// sample_kind=8: runtime frustum + configured draw-horizon result.
  uint8_t renderer_runtime_cull_visible{0};
  /// MDI pass bits: opaque, cutout, transparent (bits 0..2), separately for
  /// a resident command with indices and one whose post-cull instance count
  /// is non-zero.
  uint8_t renderer_mdi_resident_pass_flags{0};
  uint8_t renderer_mdi_visible_pass_flags{0};
  uint8_t renderer_gpu_resident_marker{0};
  uint16_t renderer_mdi_command_count{0};
  uint16_t renderer_mdi_visible_command_count{0};
  uint16_t renderer_mdi_first_block_id{0xffffu};
  uint32_t renderer_mdi_index_count{0};
  uint32_t renderer_mdi_visible_index_count{0};
  uint32_t renderer_gpu_slot_quad_count{0};
  /// sample_kind=8 CPU source-mesh cross-check for the first MDI material:
  /// counts verify face coverage while light extrema expose dark vertex data.
  uint32_t renderer_source_vertex_count{0};
  uint32_t renderer_source_index_count{0};
  uint32_t renderer_source_top_face_quads{0};
  uint32_t renderer_source_light_preview_vertices{0};
  uint8_t renderer_source_face_mask{0};
  uint8_t renderer_texture_ready{0};
  /// Opt-in payload verification: bits 0=batch, 1=pooled, 2=CPU source,
  /// 3=VBO bytes match, 4=EBO bytes match. Command bits: 0=read, 1=static
  /// fields match, 2=post-cull instanceCount matches the visibility SSBO.
  uint8_t renderer_mdi_payload_flags{0};
  uint8_t renderer_mdi_command_flags{0};
  uint32_t renderer_mdi_command_instance_count{0};
  uint32_t renderer_mdi_command_first_index{0};
  int32_t renderer_mdi_command_base_vertex{0};
  float renderer_source_sky_light_min{0.0f};
  float renderer_source_sky_light_max{0.0f};
  float renderer_source_block_light_min{0.0f};
  float renderer_source_block_light_max{0.0f};
  uint64_t renderer_pixel_probe_id{0};
  int32_t renderer_pixel_x{0};
  int32_t renderer_pixel_y{0};
  uint32_t renderer_pixel_rgba{0};
  uint32_t renderer_pixel_pretransparent_rgba{0};
  float renderer_pixel_pretransparent_depth{1.0f};
  uint8_t renderer_pixel_marker_visible{0};
  /// Valid bit plus seven-bit marker occupancy on the sampled scanline.
  uint8_t renderer_pixel_surface_valid{0};
  float renderer_pixel_surface_x{0.0f};
  float renderer_pixel_surface_y{0.0f};
  float renderer_pixel_surface_z{0.0f};
  /// Exact opaque hit reconstructed from the sampled pre-transparent depth.
  /// The existing `renderer_pixel_surface_*` fields remain the sea-plane ray
  /// intersection used to identify fluid sources.
  uint8_t renderer_pixel_opaque_surface_valid{0};
  float renderer_pixel_opaque_surface_x{0.0f};
  float renderer_pixel_opaque_surface_y{0.0f};
  float renderer_pixel_opaque_surface_z{0.0f};
  /// Optional CPU voxel-ray witness for the same screen pixel. State is
  /// 0=not sampled, 1=opaque cube hit, 2=unloaded chunk before hit,
  /// 3=no opaque cube within the renderer horizon.
  uint8_t renderer_pixel_voxel_ray_state{0};
  uint8_t renderer_pixel_voxel_ray_gap{0};
  int32_t renderer_pixel_voxel_hit_x{0};
  int32_t renderer_pixel_voxel_hit_y{0};
  int32_t renderer_pixel_voxel_hit_z{0};
  int32_t renderer_pixel_voxel_hit_block_id{-1};
  int32_t renderer_pixel_voxel_previous_block_id{-1};
  uint8_t renderer_pixel_voxel_entry_face{0xffu};
  float renderer_pixel_voxel_hit_distance{-1.0f};
  float renderer_pixel_opaque_hit_distance{-1.0f};
  int32_t renderer_pixel_voxel_chunk_x{0};
  int32_t renderer_pixel_voxel_chunk_y{0};
  int32_t renderer_pixel_voxel_chunk_z{0};
  /// Exact ownership state for the column containing the voxel-ray hit.
  /// Queue kind follows relight_queue_kind (0 none, 1 priority, 2 far,
  /// 3 keyed/no deque, 6 deferred visible, 7 deferred far).
  uint8_t renderer_pixel_voxel_chunk_pending_light{0};
  uint8_t renderer_pixel_voxel_chunk_async_relight_inflight{0};
  uint8_t renderer_pixel_voxel_chunk_relight_queue_kind{0};
  uint8_t renderer_pixel_voxel_chunk_relight_y_band_defined{0};
  int32_t renderer_pixel_voxel_chunk_relight_queue_index{-1};
  int32_t renderer_pixel_voxel_chunk_relight_queue_size{0};
  uint8_t renderer_pixel_voxel_chunk_flow_ticket_flags{0};
  uint8_t renderer_pixel_voxel_chunk_defer_until_lit{0};
  uint8_t renderer_pixel_voxel_chunk_soft_defer_held{0};
  uint8_t renderer_pixel_voxel_chunk_has_settled_light{0};
  uint64_t renderer_pixel_voxel_chunk_attempt_id{0};
  uint64_t renderer_pixel_voxel_chunk_settled_light_rev{0};
  uint64_t renderer_pixel_voxel_chunk_field_light_rev{0};
  uint32_t renderer_pixel_voxel_chunk_nonair{0};
  uint64_t renderer_pixel_voxel_chunk_mesh_revision{0};
  uint64_t renderer_pixel_voxel_chunk_published_geom_rev{0};
  uint64_t renderer_pixel_voxel_chunk_published_light_rev{0};
  /// Bits: drawable mesh, satisfying mesh, draw-ready slice, live GPU draw.
  uint8_t renderer_pixel_voxel_chunk_render_flags{0};
  /// CPU opaque, CPU transparent, packed opaque, packed transparent refs.
  uint8_t renderer_pixel_voxel_chunk_ref_flags{0};
  uint8_t renderer_pixel_voxel_chunk_column_reason{0};
  uint8_t renderer_pixel_voxel_chunk_face_debt_mask{0};
  uint16_t renderer_pixel_voxel_chunk_mdi_command_count{0};
  uint16_t renderer_pixel_voxel_chunk_mdi_visible_command_count{0};
  uint32_t renderer_pixel_voxel_chunk_mdi_index_count{0};
  uint32_t renderer_pixel_voxel_chunk_mdi_visible_index_count{0};
  uint32_t renderer_pixel_voxel_chunk_gpu_slot_quad_count{0};
  uint32_t renderer_pixel_voxel_chunk_source_index_count{0};
  /// Packed fallback status for the pixel-hit chunk: selected into the opaque
  /// fallback list, slot present, slice gate open, usable ranges/textures, and
  /// glDrawArrays calls actually issued by that list.
  uint8_t renderer_pixel_voxel_chunk_packed_draw_selected{0};
  uint8_t renderer_pixel_voxel_chunk_packed_draw_path_ready{0};
  uint8_t renderer_pixel_voxel_chunk_packed_slot_present{0};
  uint8_t renderer_pixel_voxel_chunk_packed_slice_ready{0};
  uint16_t renderer_pixel_voxel_chunk_packed_opaque_range_count{0};
  uint16_t renderer_pixel_voxel_chunk_packed_texture_ready_range_count{0};
  uint16_t renderer_pixel_voxel_chunk_packed_draw_call_count{0};
  uint32_t renderer_pixel_voxel_chunk_packed_slot_quad_count{0};
  uint32_t renderer_pixel_voxel_chunk_packed_opaque_range_quad_count{0};
  uint32_t renderer_pixel_voxel_chunk_packed_drawn_quad_count{0};
  uint32_t renderer_pixel_voxel_chunk_packed_drawn_index_count{0};
  uint32_t renderer_pixel_voxel_chunk_work_owner_flags{0};
  uint8_t renderer_pixel_voxel_chunk_dirty_queue_kind{0};
  int32_t renderer_pixel_voxel_chunk_dirty_queue_index{-1};
  int32_t renderer_pixel_voxel_chunk_dirty_queue_size{0};
  uint64_t renderer_pixel_voxel_chunk_dirty_queue_age_frames{0};
  uint8_t renderer_pixel_voxel_chunk_scheduled_this_frame{0};
  uint8_t renderer_pixel_voxel_chunk_demand_has_active_attempt{0};
  uint8_t renderer_pixel_voxel_chunk_demand_active_stage{0};
  uint64_t renderer_pixel_voxel_chunk_demand_desired_geom_rev{0};
  uint64_t renderer_pixel_voxel_chunk_demand_desired_light_rev{0};
  int32_t renderer_pixel_opaque_chunk_x{0};
  int32_t renderer_pixel_opaque_chunk_y{0};
  int32_t renderer_pixel_opaque_chunk_z{0};
  uint32_t renderer_pixel_opaque_chunk_nonair{0};
  uint64_t renderer_pixel_opaque_chunk_content_revision{0};
  uint64_t renderer_pixel_opaque_mesh_revision{0};
  uint64_t renderer_pixel_opaque_published_geom_rev{0};
  uint64_t renderer_pixel_opaque_published_light_rev{0};
  uint64_t renderer_pixel_opaque_field_light_rev{0};
  uint32_t renderer_pixel_opaque_source_index_count{0};
  /// Barycentrically interpolated source light at the closest CPU mesh
  /// triangle to the opaque depth hit; only valid within the match radius.
  uint8_t renderer_pixel_opaque_vertex_light_valid{0};
  int32_t renderer_pixel_opaque_vertex_light_block_id{-1};
  int32_t renderer_pixel_opaque_vertex_light_face_index{-1};
  float renderer_pixel_opaque_vertex_light_distance{-1.0f};
  float renderer_pixel_opaque_vertex_sky_light{0.0f};
  float renderer_pixel_opaque_vertex_block_light{0.0f};
  float renderer_pixel_opaque_vertex_light_preview{0.0f};
  uint8_t renderer_pixel_opaque_live_face_light_valid{0};
  uint8_t renderer_pixel_opaque_live_face_light_packed{0};
  uint8_t renderer_pixel_opaque_live_face_light_source{2};
  uint8_t renderer_pixel_opaque_demand_present{0};
  uint8_t renderer_pixel_opaque_demand_has_active_attempt{0};
  uint8_t renderer_pixel_opaque_demand_has_settled_light{0};
  uint8_t renderer_pixel_opaque_demand_active_stage{0};
  uint64_t renderer_pixel_opaque_demand_desired_light_rev{0};
  uint64_t renderer_pixel_opaque_demand_published_light_rev{0};
  uint64_t renderer_pixel_opaque_demand_settled_light_rev{0};
  /// Ref bits: opaque CPU, transparent CPU, packed opaque, packed transparent.
  uint8_t renderer_pixel_opaque_ref_flags{0};
  uint8_t renderer_pixel_opaque_drawable{0};
  uint8_t renderer_pixel_opaque_draw_ready{0};
  uint8_t renderer_pixel_opaque_live_gpu{0};
  /// MDI pass bits 0..2: opaque, cutout, transparent; resident vs visible.
  uint8_t renderer_pixel_opaque_mdi_resident_pass_flags{0};
  uint8_t renderer_pixel_opaque_mdi_visible_pass_flags{0};
  uint32_t renderer_pixel_opaque_mdi_index_count{0};
  uint32_t renderer_pixel_opaque_mdi_visible_index_count{0};
  /// sample_kind=3: 1=settled field needs mesh-only repair; 0=relight target.
  uint8_t draw_gate_repair_mode{0};
  /// Persistence relight queue location/band for renderer, focus, and
  /// draw-gate samples. sample_kind=2 and 8 populate exact FIFO position/band;
  /// kind 3 also emits the capture-target scan that selected the queue entry.
  /// 0=not keyed, 1=priority deque, 2=far deque, 3=keyed but absent from deque,
  /// 6=bounded exact visible target awaiting capture-dequeue promotion,
  /// 7=durable far relight awaiting FIFO admission.
  /// Other trace kinds may reuse this byte for their own queue classification.
  uint8_t relight_queue_kind{0};
  uint8_t relight_y_band_defined{0};
  int32_t relight_queue_index{-1};
  int32_t relight_queue_size{0};
  /// Focus sample queue ownership: 0=not dirty, 1=first mesh, 2=priority
  /// remesh, 3=ordinary remesh.
  uint8_t mesh_dirty_queue_kind{0};
  int32_t mesh_dirty_queue_index{-1};
  int32_t mesh_dirty_queue_size{0};
  uint64_t mesh_dirty_queue_age_frames{0};
  /// sample_kind=3/7 bits: dirty, async build, remesh-after-apply, GPU apply,
  /// GPU queued, GPU kicked/dispatched, and GPU extract owner.
  uint32_t mesh_work_owner_flags{0};
  /// sample_kind=8, 2, and 0 bitset: PendingLight map, persistence FIFO key,
  /// async relight in flight, defer-until-lit, SoftDeferHeld, column LitReady,
  /// lit gate required, ColumnFlow repair ticket (bits 0..7), and durable far
  /// relight awaiting FIFO admission (bit 8).
  uint32_t relight_owner_flags{0};
  /// ColumnFlowScheduler ticket kinds: RelightThenMesh, FirstMesh,
  /// RemeshSeam, PromoteRelight (bits 0..3).
  uint8_t column_flow_ticket_flags{0};
  uint8_t column_emerge_stage{0};
  int32_t relight_band_min_y{0};
  int32_t relight_band_max_y{-1};
  /// sample_kind=5: counts through CollectDrawGateRelightTargets filters.
  uint32_t draw_gate_scan_recent_n{0};
  uint32_t draw_gate_scan_recent_age_n{0};
  uint32_t draw_gate_scan_radius_n{0};
  uint32_t draw_gate_scan_drawable_n{0};
  uint32_t draw_gate_scan_repairable_n{0};
  uint32_t draw_gate_scan_target_n{0};
  /// sample_kind=0 bits: ticket, progress, sticky, pending_replace,
  /// column_light_revs_match, drawable, any_dark_face, dirty,
  /// remesh_after_apply, gpu_pending, inflight, column_has_stale_dark,
  /// gpu_resident, slice_stale_dark, lit_drawable, active_attempt.
  /// sample_kind=1 bits 0..8: drawable, satisfying, dirty, inflight,
  /// gpu_pending, gpu_extract, live_gpu, draw_gate_ready, remesh_after_apply.
  /// Bits 9..15 classify dark/light/column readiness. Bits 16..22 identify
  /// repair, relight, dependency queue ownership, and legal-dark settlement.
  /// Bits 23..25 identify OpenSky, LightRepair, and true-dark state.
  /// Bits 26..27 split GPU apply into queued and kicked/dispatched phases.
  /// Bits 28..31 identify live ColumnFlow tickets by work kind.
  uint32_t flags{0};
};

class UJobStageTrace
{
public:
  static constexpr size_t kRingCapacity = 256;
  /// Bounded worker/GPU lifecycle history retained for sampled visible slices.
  static constexpr size_t kVisualLifecycleRingCapacity = 8192;
  /// Bounded demand-owner transitions for slices sampled by the renderer trace.
  static constexpr size_t kDemandTransitionRingCapacity = 16384;
  static constexpr size_t kCullDecisionRingCapacity = 64;
  static constexpr size_t kVisualBlackTraceRingCapacity = 1024;
  static constexpr size_t kVisualPixelTraceRingCapacity = 2048;
  static constexpr size_t kRendererGateTraceRingCapacity = 4096;
  static constexpr size_t kFrustumCoverageTraceRingCapacity = 256;
  static constexpr size_t kVisualBlackAttributionTraceRingCapacity = 1024;
  static constexpr size_t kVisualRepairTraceRingCapacity = 2048;
  static constexpr size_t kMeshScheduleTraceRingCapacity = 1024;
  static constexpr size_t kPriorityRemeshTraceRingCapacity = 2048;
  static constexpr size_t kVisualBlackTraceDumpCapacity =
      kVisualBlackTraceRingCapacity +
      kRendererGateTraceRingCapacity +
      kFrustumCoverageTraceRingCapacity +
      kVisualBlackAttributionTraceRingCapacity +
      kVisualRepairTraceRingCapacity + kMeshScheduleTraceRingCapacity +
      kPriorityRemeshTraceRingCapacity + kVisualPixelTraceRingCapacity;

  static void Note(const JobStageSpan &span);
  /// Record the final retirement/cancellation reason and elapsed job age.
  static void NoteTerminal(JobStageSpan span, JobStage terminal_stage,
                           JobTerminalReason reason);
  /// Retain lifecycle events for a chunk selected by the opt-in frustum trace.
  static void WatchVisualChunk(int32_t cx, int32_t cy, int32_t cz);
  static void ForEachWatchedNewest(
      size_t max_n, void (*fn)(const JobStageSpan &, void *), void *ctx);
  static void NoteDemandTransition(const DemandTransitionSpan &span);
  static void NoteMeshRevisionBump(DemandTransitionSpan span,
                                   uint64_t revision_before,
                                   uint64_t revision_after,
                                   MeshRevisionBumpReason reason,
                                   uint32_t owner_flags);
  static void ForEachDemandTransitionNewest(
      size_t max_n, void (*fn)(const DemandTransitionSpan &, void *),
      void *ctx);
  static size_t Size();
  static bool Get(size_t newest_index, JobStageSpan &out);
  /// Emit compact JSONL lines (kind=job_trace) into an open ostream-like sink
  /// via callback; used by FramePerfMonitor emergency dump.
  static void ForEachNewest(size_t max_n,
                            void (*fn)(const JobStageSpan &, void *), void *ctx);
  /// Cull decisions are high volume and must not evict lifecycle transitions.
  static void ForEachCullDecisionNewest(
      size_t max_n, void (*fn)(const JobStageSpan &, void *), void *ctx);
  static bool VisualBlackTraceEnabled();
  static void NoteVisualBlack(const VisualBlackTraceRecord &record);
  /// Dump each trace class from its own bounded ring. max_n is applied per
  /// class so high-rate view samples cannot evict repair/schedule evidence.
  static void ForEachVisualBlackNewest(
      size_t max_n, void (*fn)(const VisualBlackTraceRecord &, void *),
      void *ctx);
  static const char *StageName(JobStage s);
  static const char *DemandTransitionName(DemandTransitionKind kind);
  static const char *MeshRevisionBumpReasonName(
      MeshRevisionBumpReason reason);
  static const char *TerminalReasonName(JobTerminalReason reason);
  /// A36 S1: note cull exclusion for a tracked chunk (bounded ring).
  static void NoteCullDecision(int32_t cx, int32_t cy, int32_t cz,
                               uint8_t cull_decision, uint64_t attempt_id = 0,
                               uint64_t published_rev = 0);
};

} // namespace cutum

#endif
