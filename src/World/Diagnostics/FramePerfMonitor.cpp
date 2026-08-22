#include "World/Diagnostics/FramePerfMonitor.h"

#include "App/Core.h"
#include "Render/Engine/MdiVertexPoolStore.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Pipeline/GpuTransparentSort.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/World.h"
#include "World/Lighting/GpuBlocklightFlood.h"
#include "Render/Backend/GpuHotPathFallback.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "Render/Mesh/GpuGreedyMesher.h"
#include "World/Physics/PhysicsTelemetry.h"
#include "World/Streaming/UnderfeetTelemetryPolicy.h"
#include "glog/logging.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <unistd.h>
#endif

namespace cutum
{
namespace
{

struct Session
{
  std::mutex Mutex;
  std::ofstream Jsonl;
  std::string Path;
  bool Opened{false};
  double AccumWallMs{0.0};
  double AccumSimMs{0.0};
  double AccumSwapMs{0.0};
  double AccumUnaccountedMs{0.0};
  double AccumInputMs{0.0};
  double AccumAppUpdateMs{0.0};
  double AccumWorldExtraMs{0.0};
  double AccumPrepareMs{0.0};
  double AccumPostSceneMs{0.0};
  double AccumGuiMs{0.0};
  double AccumResidualMs{0.0};
  double AccumFluidCpuMs{0.0};
  double AccumFluidGpuMs{0.0};
  double AccumStreamMs{0.0};
  double AccumMeshEmergeMs{0.0};
  double AccumWorldStreamingPhaseMs{0.0};
  double AccumSceneMs{0.0};
  double AccumPhysMs{0.0};
  double MaxWallMs{0.0};
  double MaxStreamMs{0.0};
  double MaxMeshEmergeMs{0.0};
  double MaxPhysMs{0.0};
  int MaxDirty{0};
  int MaxRingBlocked{0};
  int MaxPendingLight{0};
  int SpikesWrittenThisPeriod{0};
  int FrameCount{0};
  uint64_t MeshApplyStaleAtPeriodStart{0};
  uint64_t MeshCompletedDiscardedAtPeriodStart{0};
  uint64_t SoftDeferCaptureFloorHitsAtPeriodStart{0};
  uint64_t SoftDeferWitnessRetargetAtPeriodStart{0};
  std::chrono::steady_clock::time_point LastEmit{
      std::chrono::steady_clock::now()};
  double LastRssMb{0.0};
  double LastPrivateMb{0.0};
  int FramesSinceMemSample{30};
  double AccumPerfCollectMs{0.0};
  double AccumPerfEmitMs{0.0};
};

Session &GetSession()
{
  static Session s;
  return s;
}

void OpenSessionLocked(Session &s)
{
  if (s.Opened)
  {
    return;
  }
  const auto logs = GetExecutableDirectory() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(logs, ec);
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream name;
#ifdef _WIN32
  const long long pid = static_cast<long long>(GetCurrentProcessId());
#else
  const long long pid = static_cast<long long>(getpid());
#endif
  name << "perf_" << std::put_time(&tm, "%Y%m%d-%H%M%S") << "_" << pid
       << ".jsonl";
  s.Path = (logs / name.str()).string();
  s.Jsonl.open(s.Path, std::ios::out | std::ios::app);
  s.Opened = s.Jsonl.is_open();
  if (s.Opened)
  {
    LOG(INFO) << "[Perf] session file=" << s.Path;
  }
  else
  {
    LOG(WARNING) << "[Perf] failed to open " << s.Path;
  }
}

struct FrameNumbers
{
  double wall_ms{0.0};
  double sim_ms{0.0};
  double swap_wait_ms{0.0};
  double unaccounted_ms{0.0};
  double phys_ms{0.0};
  double stream_ms{0.0};
  double mesh_emerge_ms{0.0};
  double scene_ms{0.0};
  double view_ms{0.0};
  double flat_ms{0.0};
  double input_ms{0.0};
  double app_update_ms{0.0};
  double world_extra_ms{0.0};
  double views_ms{0.0};
  double do_movement_ms{0.0};
  double ensure_collision_ms{0.0};
  double creature_tick_ms{0.0};
  double camera_move_ms{0.0};
  double camera_ground_support_ms{0.0};
  double camera_locomotion_ms{0.0};
  double camera_horiz_move_ms{0.0};
  double camera_sync_ms{0.0};
  double environment_tick_ms{0.0};
  double npc_intent_ms{0.0};
  double controlled_influence_ms{0.0};
  double vitals_tick_ms{0.0};
  double status_effects_tick_ms{0.0};
  int creatures_total{0};
  int creatures_ai_ticked{0};
  int world_creatures_skipped{0};
  double player_locomotion_block_ms{0.0};
  double world_ai_after_player_ms{0.0};
  int creatures_ai_budget{0};
  int creatures_ai_deferred{0};
  float stream_speed_clamp_scale{1.0f};
  double world_streaming_phase_ms{0.0};
  double block_input_ms{0.0};
  double tick_env_ms{0.0};
  double physics_block_ms{0.0};
  double physics_drain_ms{0.0};
  double physics_movement_ms{0.0};
  int physics_substeps{0};
  int break_complete_n{0};
  int break_inflight_race_n{0};
  int break_dark_face_n{0};
  int place_complete_n{0};
  int place_emission_n{0};
  int edit_light_emission{0};
  double edit_to_first_mesh_ms{0.0};
  double fast_relight_ms{0.0};
  double prepare_frame_ms{0.0};
  double post_scene_ms{0.0};
  double gui_overlay_ms{0.0};
  double autosave_ms{0.0};
  int autosave_deferred_n{0};
  int autosave_skipped_tick_n{0};
  int dig_seam_pending_n{0};
  int dig_seam_remesh_n{0};
  int stale_repair_wave_n{0};
  int stand_rim_dirty_n{0};
  int stand_rim_imm_n{0};
  double render_total_ms{0.0};
  double residual_ms{0.0};
  double perf_collect_ms{0.0};
  double perf_emit_ms{0.0};
  double fluid_map_cpu_ms{0.0};
  double fluid_map_gpu_ms{0.0};
  int fluid_map_dirty{0};
  int fluid_map_full_rebuild{0};
  double commit_apply_ms{0.0};
  double commit_seal_ms{0.0};
  double commit_physics_ms{0.0};
  double streamer_update_ms{0.0};
  double async_io_ms{0.0};
  double relight_drain_ms{0.0};
  double relight_capture_ms{0.0};
  double relight_apply_ms{0.0};
  int relight_fifo_drop_n{0};
  int relight_fifo_pin_saved_n{0};
  double mesh_sync_ms{0.0};
  double mesh_snapshot_ms{0.0};
  double mesh_immediate_ms{0.0};
  int mesh_immediate_count{0};
  double mesh_dirty_tick_ms{0.0};
  double mesh_dirty_prune_ms{0.0};
  int mesh_dirty_prune_n{0};
  double mesh_dirty_sort_ms{0.0};
  double mesh_dirty_drain_ms{0.0};
  int mesh_dirty_drain_n{0};
  double mesh_dirty_schedule_ms{0.0};
  int mesh_dirty_schedule_ok_n{0};
  int mesh_dirty_schedule_skip_n{0};
  double mesh_dirty_gpu_ms{0.0};
  int mesh_dirty_gpu_n{0};
  double mesh_dirty_sync_ms{0.0};
  int mesh_dirty_sync_n{0};
  int dirty_touch_n{0};
  int dirty_revisit_same_n{0};
  int dirty_fm_n{0};
  int dirty_remesh_n{0};
  int prep_unfinished_calls_n{0};
  int prep_unfinished_full_n{0};
  int prep_unfinished_incremental_n{0};
  int unfinished_cache_hit_n{0};
  int unfinished_cache_overflow_n{0};
  int dirty_admit_budget_end{0};
  int first_mesh_schedule_cap{0};
  int remesh_schedule_cap{0};
  int relight_trim_far_n{0};
  float player_x{0.0f};
  float player_y{0.0f};
  float player_z{0.0f};
  int phase_budget_over{0};
  int phase_miss_carve_out{0};
  double miss_reserved_ms{0.0};
  double emerge_budget_ms{0.0};
  int render_preset{0};
  int async_meshing{0};
  double mesh_emerge_prep_ms{0.0};
  double mesh_emerge_prep_missing_ms{0.0};
  double mesh_emerge_prep_unfinished_ms{0.0};
  double mesh_emerge_prep_sticky_ms{0.0};
  double mesh_emerge_prep_drop_dirty_ms{0.0};
  double mesh_emerge_prep_other_ms{0.0};
  double prep_pending_light_ms{0.0};
  double prep_black_sticky_ms{0.0};
  double prep_dirty_count_ms{0.0};
  double prep_softdefer_setup_ms{0.0};
  double softdefer_empty_scan_ms{0.0};
  double softdefer_empty_own_ms{0.0};
  int keep_cols{0};
  int visual_cols{0};
  double idle_prefetch_ms{0.0};
  int prefetch_visual_ops{0};
  int prefetch_keep_ops{0};
  int gen_backlog_total{0};
  int gen_q{0};
  int mesh_async{0};
  int dirty{0};
  int stream_loads{0};
  int stream_async_queued{0};
  int stream_disk_complete_n{0};
  int stream_gen_commit_n{0};
  int frontier_pressure{0};
  int stream_ring_blocked{0};
  int stream_near_skipped{0};
  int stream_load_candidates{0};
  int allow_proc_fill{0};
  int column_absent_in_rd_n{0};
  int column_loaded_no_mesh_n{0};
  int column_bump_denied{0};
  int column_flow_upgrade_n{0};
  int column_lighting_n{0};
  int column_meshing_n{0};
  int column_render_ready_n{0};
  int pending_light{0};
  int stream_pressure{0};
  int pending_light_focus{0};
  int focus_cx{0};
  int focus_cz{0};
  int underfeet_need{0};
  int underfeet_draw_ok{0};
  int underfeet_has_mesh{0};
  int underfeet_sticky{0};
  int underfeet_pending_light{0};
  int underfeet_reason{0};
  int underfeet_stage{0};
  int underfeet_opaque_present{0};
  int underfeet_opaque_present_raw{0};
  int underfeet_opaque_present_predicted{0};
  int lighting_relight_deferred{0};
  int fog_pull_in_rd{0};
  int fog_pull_in_margin{0};
  float fog_pull_in_start_ratio{0.0f};
  int fog_hole_debt{0};
  int near_focus_holes{0};
  int visual_holes{0};
  int unfinished_visual{0};
  int light_debt{0};
  int focus_missing_mesh{0};
  int focus_dark_mesh{0};
  int focus_pending_dark{0};
  int focus_sticky_remesh{0};
  int visible_black_focus_n{0};
  int visible_black_no_ticket_n{0};
  int visible_black_progress_n{0};
  int visible_black_stalled_n{0};
  int focus_not_render_ready{0};
  int focus_pressure{0};
  int focus_dirty_chunks{0};
  int focus_unfinished_ahead{0};
  int focus_unfinished_behind{0};
  uint64_t mesh_discarded_late{0};
  uint64_t mesh_apply_stale{0};
  uint64_t mesh_apply_stale_delta{0};
  uint64_t mesh_completed_discarded_delta{0};
  uint64_t mesh_replace_hole_avoided{0};
  int pending_gpu_applies_n{0};
  int pending_gpu_queued_n{0};
  int pending_gpu_kicked_n{0};
  int gpu_kick_n{0};
  int gpu_finish_n{0};
  int gpu_finish_not_ready_n{0};
  int mesh_schedule_final{0};
  int mesh_drain_final{0};
  int mesh_admission_mode{0};
  int miss_cx{0};
  int miss_cy{0};
  int miss_cz{0};
  int miss_horiz{0};
  int post_load_ring_not_ready{0};
  int enter_game_warmup_missing_greedy{0};
  uint64_t softdefer_capture_floor_hits{0};
  uint64_t softdefer_capture_floor_hits_delta{0};
  uint64_t softdefer_witness_retarget{0};
  uint64_t softdefer_witness_retarget_delta{0};
  int softdefer_witness_horiz{0};
  int softdefer_capture_budget{0};
  int frame_budget_ms{0};
  int capture_over_budget{0};
  int heal_deferred_for_miss{0};
  uint64_t stage_skip_remesh_pending_light{0};
  int softdefer_empty_placeholder_n{0};
  int softdefer_empty_stuck_n{0};
  int softdefer_empty_stuck_cx{0};
  int softdefer_empty_stuck_cy{0};
  int softdefer_empty_stuck_cz{0};
  int softdefer_empty_stuck_horiz{0};
  int softdefer_empty_age_max_frames{0};
  int softdefer_empty_owned_n{0};
  uint64_t softdefer_empty_publish_avoided{0};
  int softdefer_held_n{0};
  double rss_mb{0.0};
  double private_mb{0.0};
  int chunk_count{0};
  uint64_t greedy_vertices{0};
  int mesh_completed_n{0};
  int mesh_completed_cap{0};
  uint64_t mesh_completed_discarded{0};
  int relight_completed_n{0};
  int relight_completed_cap{0};
  uint64_t relight_completed_discarded{0};
  int relight_capture_col_horiz{-1};
  int relight_capture_finalize{0};
  int relight_capture_band_cy_span{0};
  int relight_capture_full_n{0};
  int relight_capture_neighbor_light_n{0};
  int relight_witness_hold_n{0};
  int relight_apply_n{0};
  int relight_apply_partial_n{0};
  int relight_apply_final_n{0};
  int relight_deferred_far_pending{0};
  uint64_t relight_deferred_far_enqueue_n{0};
  int mark_relit_skip_already_dirty_n{0};
  int mark_relit_skip_already_raa_n{0};
  int mark_relit_skip_inflight_n{0};
  int mark_relit_skip_enter_lit_quiesce_n{0};
  int mark_relit_schedule_n{0};
  int mark_relit_suppress_enter_settled_n{0};
  int sticky_insert_stale_after_apply_n{0};
  int sticky_insert_seam_n{0};
  int sticky_insert_other_n{0};
  int sticky_erase_drawable_n{0};
  int sticky_erase_pending_clear_n{0};
  int sticky_erase_pruned_far_n{0};
  int sticky_erase_remesh_commit_n{0};
  int sticky_erase_other_n{0};
  int dirty_n{0};
  int pending_light_n{0};
  int relight_fifo_n{0};
  uint64_t dirty_dropped{0};
  uint64_t pending_light_dropped{0};
  uint64_t relight_fifo_dropped{0};
  uint64_t relight_false_clear_n{0};
  double gpu_pool_used_mb{0.0};
  double gpu_pool_cap_mb{0.0};
  uint64_t gpu_draw_cmds{0};
  double gpu_cull_ms{0.0};
  double vertex_pool_fill{0.0};
  double gpu_cull_indirect{0.0};
  uint64_t opaque_cmd_total{0};
  uint64_t opaque_cmd_on{0};
  uint64_t opaque_gpu_packed_n{0};
  uint64_t opaque_draw_n{0};
  uint64_t opaque_refs_cpu_vis{0};
  uint64_t opaque_refs_render_ready{0};
  uint64_t opaque_mdi_eligible{0};
  uint64_t cross_batch_count{0};
  uint64_t cpu_aabb_would_on{0};
  uint64_t edit_immediate_n{0};
  uint64_t edit_dirty_n{0};
  uint64_t edit_neighbor_pending_frames{0};
  uint64_t pool_unsync_uploads{0};
  double pool_fence_wait_ms{0.0};
  uint64_t chunk_meshed_culled0{0};
  uint64_t chunk_meshed_unlit{0};
  uint64_t chunk_meshed_unlit_hidden{0};
  uint64_t chunk_meshed_unlit_preview{0};
  uint64_t chunk_not_ready{0};
  int dark_face_near_n{0};
  int dark_face_stale_near_n{0};
  int dark_face_void_near_n{0};
  int dark_face_bx{0};
  int dark_face_by{0};
  int dark_face_bz{0};
  int dark_face_cx{0};
  int dark_face_cy{0};
  int dark_face_cz{0};
  int dark_face_block_id{0};
  int dark_face_index{0};
  double dark_face_dist{0.0};
  uint64_t gpu_mesh_vbo_dispatch{0};
  uint64_t gpu_light_seed_apply{0};
  uint64_t gpu_mask_readback{0};
  uint64_t gpu_transparent_sort_readback{0};
  uint64_t gpu_cull_stats_readback{0};
  double gpu_cull_cpu_ms{0.0};
  double gpu_cull_gpu_ms{0.0};
  uint64_t gpu_blocklight_flood{0};
  uint64_t gpu_fluid_readback{0};
  uint64_t gpu_light_readback{0};
  uint64_t gpu_opaque_emit_gpu{0};
  uint64_t gpu_transparent_sort_gpu{0};
  uint64_t gpu_fallback{0};
  double gpu_fluid_scan_on{0.0};
  std::string backend_mesher;
  std::string backend_store;
  std::string backend_cull;
  std::string backend_fluid;
  std::string backend_lighting_mode;
  double caps_has_compute{0.0};
  double caps_has_ssbo{0.0};
  double caps_probe_completed{0.0};
  double android_gpu_user_pref{0.0};
  double android_gpu_effective{0.0};
  std::string android_gpu_deny_reason;
  std::string gl_version;
  std::string gl_renderer;
  int memory_pressure{0};
  int keep_margin_eff{0};
  uint64_t buffer_expand_events{0};
  std::string pending_cols;
  double max_wall_ms{0.0};
  double max_stream_ms{0.0};
  double max_mesh_emerge_ms{0.0};
  double max_phys_ms{0.0};
  int max_dirty{0};
  int max_ring_blocked{0};
  int max_pending_light{0};
  int frames{0};
  int spikes{0};
};

FrameNumbers Compute(UWorld &world, double swap_wait_ms, double frame_wall_ms,
                     Session &s)
{
  FrameNumbers n;
  const PhysicsTelemetry &phys = world.GetPhysicsTelemetry();
  n.wall_ms = frame_wall_ms > 0.0 ? frame_wall_ms
                                  : world.GetWallFrameDelta() * 1000.0;
  n.phys_ms = phys.PhysicsStepMs;
  n.stream_ms = phys.StreamMs;
  n.mesh_emerge_ms = phys.MeshEmergeMs;
  n.scene_ms = world.GetDurationDrawSceneMks() / 1000.0;
  n.view_ms = world.GetDurationViewUpdateMks() / 1000.0;
  // Era14: DoMovement is locomotion-only; stream/emerge live in
  // TickWorldStreamingPhase (WorldStreamingPhaseMs / stream_ms+mesh_emerge_ms).
  n.do_movement_ms = phys.DoMovementMs;
  n.ensure_collision_ms = phys.EnsureCollisionMs;
  n.creature_tick_ms = phys.CreatureTickMs;
  n.camera_move_ms = phys.CameraDoMovementMs;
  n.camera_ground_support_ms = phys.CameraGroundSupportMs;
  n.camera_locomotion_ms = phys.CameraLocomotionMs;
  n.camera_horiz_move_ms = phys.CameraHorizMoveMs;
  n.camera_sync_ms = phys.CameraSyncMs;
  n.environment_tick_ms = phys.EnvironmentTickMs;
  n.npc_intent_ms = phys.NpcIntentExecuteMs;
  n.controlled_influence_ms = phys.ControlledInfluenceMs;
  n.vitals_tick_ms = phys.VitalsTickMs;
  n.status_effects_tick_ms = phys.StatusEffectsTickMs;
  n.creatures_total = phys.CreaturesTotal;
  n.creatures_ai_ticked = phys.CreaturesAiTicked;
  n.world_creatures_skipped = phys.WorldCreaturesSkipped;
  n.player_locomotion_block_ms = phys.PlayerLocomotionBlockMs;
  n.world_ai_after_player_ms = phys.WorldAiAfterPlayerMs;
  n.creatures_ai_budget = phys.CreaturesAiBudget;
  n.creatures_ai_deferred = phys.CreaturesAiDeferred;
  n.stream_speed_clamp_scale = phys.StreamSpeedClampScale;
  n.world_streaming_phase_ms = phys.WorldStreamingPhaseMs;
  n.input_ms = world.GetLastInputMs();
  n.app_update_ms = world.GetLastAppUpdateMs();
  n.views_ms = phys.ViewsMs;
  n.swap_wait_ms = swap_wait_ms;
  n.block_input_ms = phys.BlockInputMs;
  n.tick_env_ms = phys.TickEnvMs;
  n.physics_block_ms = phys.BlockStepMs;
  n.physics_drain_ms = phys.DrainStepMs;
  n.physics_movement_ms = phys.MovementStepMs;
  n.physics_substeps = phys.PhysicsSubsteps;
  n.break_complete_n = phys.BreakCompleteN;
  n.break_inflight_race_n = phys.BreakInflightRaceN;
  n.break_dark_face_n = phys.BreakDarkFaceN;
  n.place_complete_n = phys.PlaceCompleteN;
  n.place_emission_n = phys.PlaceEmissionN;
  n.edit_light_emission = phys.EditLightEmission;
  n.edit_to_first_mesh_ms =
      (phys.BreakCompleteN > 0 || phys.PlaceCompleteN > 0)
          ? phys.EditToFirstMeshMs
          : 0.0;
  n.fast_relight_ms =
      (phys.BreakCompleteN > 0 || phys.PlaceCompleteN > 0) ? phys.FastRelightMs
                                                            : 0.0;
  n.prepare_frame_ms = world.GetLastPrepareFrameMs();
  n.post_scene_ms = world.GetLastPostSceneMs();
  n.gui_overlay_ms = world.GetLastGuiOverlayMs();
  n.autosave_ms = world.GetLastAutosaveMs();
  n.autosave_deferred_n = phys.AutosaveDeferredN;
  n.autosave_skipped_tick_n = phys.AutosaveSkippedTickN;
  n.dig_seam_pending_n = phys.DigSeamPendingN;
  n.dig_seam_remesh_n = phys.DigSeamRemeshN;
  n.stale_repair_wave_n = phys.StaleRepairWaveN;
  n.stand_rim_dirty_n = phys.StandRimDirtyN;
  n.stand_rim_imm_n = phys.StandRimImmN;
  n.render_total_ms = world.GetLastRenderTotalMs();
  // sim_ms = main-loop work excluding swap. Era14: do_movement is locomotion;
  // stream/emerge are in world_streaming_phase_ms (also mirrored as stream_ms /
  // mesh_emerge_ms — do not add those again).
  n.sim_ms = n.input_ms + n.app_update_ms + n.views_ms + n.do_movement_ms +
             n.world_streaming_phase_ms + n.block_input_ms + n.autosave_ms +
             n.render_total_ms;
  n.unaccounted_ms = n.wall_ms - n.sim_ms - n.swap_wait_ms;
  if (n.unaccounted_ms < 0.0 && n.unaccounted_ms > -1.0)
  {
    n.unaccounted_ms = 0.0;
  }
  n.world_extra_ms = (std::max)(
      0.0, world.GetLastWorldTickMs() - n.do_movement_ms -
               n.world_streaming_phase_ms);
  n.residual_ms = n.unaccounted_ms - n.world_extra_ms;
  n.fluid_map_cpu_ms = world.GetLastFluidMapCpuMs();
  n.fluid_map_gpu_ms = world.GetLastFluidMapGpuMs();
  n.fluid_map_dirty = world.GetLastFluidMapDirtyChunks();
  n.fluid_map_full_rebuild = world.GetLastFluidMapFullRebuild() ? 1 : 0;
  n.commit_apply_ms = phys.CommitApplyMs;
  n.commit_seal_ms = phys.CommitSealMs;
  n.commit_physics_ms = phys.CommitPhysicsMs;
  n.streamer_update_ms = phys.StreamerUpdateMs;
  n.async_io_ms = phys.AsyncIoMs;
  n.relight_drain_ms = phys.RelightDrainMs;
  n.relight_capture_ms = phys.RelightCaptureMs;
  n.relight_apply_ms = phys.RelightApplyMs;
  n.relight_fifo_drop_n = phys.RelightFifoDropN;
  n.relight_fifo_pin_saved_n = phys.RelightFifoPinSavedN;
  n.mesh_sync_ms = phys.MeshSyncMs;
  n.mesh_snapshot_ms = phys.MeshSnapshotMs;
  n.mesh_immediate_ms = phys.MeshImmediateMs;
  n.mesh_immediate_count = phys.MeshImmediateCount;
  n.mesh_dirty_tick_ms = phys.MeshDirtyTickMs;
  n.mesh_dirty_prune_ms = phys.MeshDirtyPruneMs;
  n.mesh_dirty_prune_n = phys.MeshDirtyPruneN;
  n.mesh_dirty_sort_ms = phys.MeshDirtySortMs;
  n.mesh_dirty_drain_ms = phys.MeshDirtyDrainMs;
  n.mesh_dirty_drain_n = phys.MeshDirtyDrainN;
  n.mesh_dirty_schedule_ms = phys.MeshDirtyScheduleMs;
  n.mesh_dirty_schedule_ok_n = phys.MeshDirtyScheduleOkN;
  n.mesh_dirty_schedule_skip_n = phys.MeshDirtyScheduleSkipN;
  n.mesh_dirty_gpu_ms = phys.MeshDirtyGpuMs;
  n.mesh_dirty_gpu_n = phys.MeshDirtyGpuN;
  n.mesh_dirty_sync_ms = phys.MeshDirtySyncMs;
  n.mesh_dirty_sync_n = phys.MeshDirtySyncN;
  n.dirty_touch_n = phys.DirtyTouchN;
  n.dirty_revisit_same_n = phys.DirtyRevisitSameN;
  n.dirty_fm_n = phys.DirtyFmN;
  n.dirty_remesh_n = phys.DirtyRemeshN;
  n.prep_unfinished_calls_n = phys.PrepUnfinishedCallsN;
  n.prep_unfinished_full_n = phys.PrepUnfinishedFullN;
  n.prep_unfinished_incremental_n = phys.PrepUnfinishedIncrementalN;
  n.unfinished_cache_hit_n = phys.UnfinishedCacheHitN;
  n.unfinished_cache_overflow_n = phys.UnfinishedCacheOverflowN;
  n.dirty_admit_budget_end = phys.DirtyAdmitBudgetEnd;
  n.first_mesh_schedule_cap = phys.FirstMeshScheduleCap;
  n.remesh_schedule_cap = phys.RemeshScheduleCap;
  n.relight_trim_far_n = phys.RelightTrimFarN;
  n.player_x = phys.PlayerX;
  n.player_y = phys.PlayerY;
  n.player_z = phys.PlayerZ;
  n.phase_budget_over = phys.PhaseBudgetOver;
  n.phase_miss_carve_out = phys.PhaseMissCarveOut;
  n.miss_reserved_ms = phys.MissReservedMs;
  n.emerge_budget_ms = phys.EmergeBudgetCapMs;
  {
    const auto &rs = world.GetRenderSettings();
    n.render_preset = static_cast<int>(rs.Preset);
    n.async_meshing = rs.AsyncMeshing ? 1 : 0;
  }
  n.mesh_emerge_prep_ms = phys.MeshEmergePrepMs;
  n.mesh_emerge_prep_missing_ms = phys.MeshEmergePrepMissingMs;
  n.mesh_emerge_prep_unfinished_ms = phys.MeshEmergePrepUnfinishedMs;
  n.mesh_emerge_prep_sticky_ms = phys.MeshEmergePrepStickyMs;
  n.mesh_emerge_prep_drop_dirty_ms = phys.MeshEmergePrepDropDirtyMs;
  n.mesh_emerge_prep_other_ms = phys.MeshEmergePrepOtherMs;
  n.prep_pending_light_ms = phys.PrepPendingLightMs;
  n.prep_black_sticky_ms = phys.PrepBlackStickyMs;
  n.prep_dirty_count_ms = phys.PrepDirtyCountMs;
  n.prep_softdefer_setup_ms = phys.PrepSoftdeferSetupMs;
  n.softdefer_empty_scan_ms = phys.SoftdeferEmptyScanMs;
  n.softdefer_empty_own_ms = phys.SoftdeferEmptyOwnMs;
  n.keep_cols = phys.KeepCols;
  n.visual_cols = phys.VisualCols;
  n.idle_prefetch_ms = phys.IdlePrefetchMs;
  n.prefetch_visual_ops = phys.PrefetchVisualOps;
  n.prefetch_keep_ops = phys.PrefetchKeepOps;
  n.gen_backlog_total = phys.GenBacklogTotal;
  const auto &md = world.GetMovementDiagnostics();
  n.flat_ms = md.flatRebuildMs;
  n.gen_q = md.genQueuePending;
  n.mesh_async = md.asyncMeshInFlight;
  n.dirty = md.dirtyChunksPending;
  n.stream_loads = phys.StreamLoads;
  n.stream_async_queued = phys.StreamAsyncQueued;
  n.stream_disk_complete_n = phys.StreamDiskCompleteN;
  n.stream_gen_commit_n = phys.StreamGenCommitN;
  n.frontier_pressure = phys.FrontierPressure;
  n.stream_ring_blocked = phys.StreamRingBlocked;
  n.stream_near_skipped = phys.StreamNearSkipped;
  n.stream_load_candidates = phys.StreamLoadCandidates;
  n.allow_proc_fill = phys.AllowProcFill;
  n.column_absent_in_rd_n = phys.ColumnAbsentInRdN;
  n.column_loaded_no_mesh_n = phys.ColumnLoadedNoMeshN;
  n.column_bump_denied = phys.ColumnBumpDenied;
  n.column_flow_upgrade_n = phys.ColumnFlowUpgradeN;
  n.column_lighting_n = phys.ColumnLightingN;
  n.column_meshing_n = phys.ColumnMeshingN;
  n.column_render_ready_n = phys.ColumnRenderReadyN;
  n.pending_light = phys.PendingLightCount;
  n.stream_pressure = phys.StreamPressure;
  n.pending_light_focus = phys.PendingLightFocus;
  n.focus_cx = phys.FocusChunkX;
  n.focus_cz = phys.FocusChunkZ;
  n.underfeet_need = phys.UnderfeetNeed;
  n.underfeet_draw_ok = phys.UnderfeetDrawOk;
  n.underfeet_has_mesh = phys.UnderfeetHasMesh;
  n.underfeet_sticky = phys.UnderfeetSticky;
  n.underfeet_pending_light = phys.UnderfeetPendingLight;
  n.underfeet_reason = phys.UnderfeetReason;
  n.underfeet_stage = phys.UnderfeetStage;
  n.underfeet_opaque_present_raw = phys.UnderfeetOpaquePresentRaw;
  n.underfeet_opaque_present_predicted = phys.UnderfeetOpaquePresentPredicted;
  n.underfeet_opaque_present = UnderfeetOpaquePresentForPerf(
      phys.UnderfeetDrawOk != 0, phys.UnderfeetOpaquePresentLatched != 0,
      phys.UnderfeetOpaquePresentPredicted != 0);
  n.lighting_relight_deferred = phys.LightingRelightDeferred;
  n.fog_pull_in_rd = phys.FogPullInRd;
  n.fog_pull_in_margin = phys.FogPullInMargin;
  n.fog_pull_in_start_ratio = phys.FogPullInStartRatio;
  n.fog_hole_debt = phys.FogHoleDebt;
  n.near_focus_holes = phys.NearFocusHoles;
  n.visual_holes = phys.VisualHoles;
  n.unfinished_visual = phys.UnfinishedVisual;
  n.light_debt = phys.LightDebt;
  n.focus_missing_mesh = phys.FocusMissingMesh;
  n.miss_cx = phys.MissCx;
  n.miss_cy = phys.MissCy;
  n.miss_cz = phys.MissCz;
  n.miss_horiz = phys.MissHoriz;
  n.focus_dark_mesh = phys.FocusDarkMesh;
  n.focus_pending_dark = phys.FocusPendingDark;
  n.focus_sticky_remesh = phys.FocusStickyRemesh;
  n.visible_black_focus_n = phys.VisibleBlackFocusN;
  n.visible_black_no_ticket_n = phys.VisibleBlackNoTicketN;
  n.visible_black_progress_n = phys.VisibleBlackProgressN;
  n.visible_black_stalled_n = phys.VisibleBlackStalledN;
  n.focus_not_render_ready = phys.FocusNotRenderReady;
  n.focus_pressure = phys.FocusPressure;
  n.focus_dirty_chunks = phys.FocusDirtyChunks;
  n.focus_unfinished_ahead = phys.FocusUnfinishedAhead;
  n.focus_unfinished_behind = phys.FocusUnfinishedBehind;
  n.mesh_discarded_late = phys.MeshDiscardedLate;
  n.mesh_apply_stale = phys.MeshApplyStale;
  n.mesh_replace_hole_avoided = phys.MeshReplaceHoleAvoided;
  n.pending_gpu_applies_n = phys.PendingGpuAppliesN;
  n.pending_gpu_queued_n = phys.PendingGpuQueuedN;
  n.pending_gpu_kicked_n = phys.PendingGpuKickedN;
  n.gpu_kick_n = phys.GpuKickN;
  n.gpu_finish_n = phys.GpuFinishN;
  n.gpu_finish_not_ready_n = phys.GpuFinishNotReadyN;
  n.mesh_schedule_final = phys.MeshScheduleFinal;
  n.mesh_drain_final = phys.MeshDrainFinal;
  n.mesh_admission_mode = phys.MeshAdmissionMode;
  n.post_load_ring_not_ready = phys.PostLoadRingNotReady;
  n.enter_game_warmup_missing_greedy = phys.EnterGameWarmupMissingGreedy;
  n.softdefer_capture_floor_hits = phys.SoftDeferCaptureFloorHits;
  n.softdefer_witness_retarget = phys.SoftDeferWitnessRetarget;
  n.softdefer_witness_horiz = phys.SoftDeferWitnessHoriz;
  n.softdefer_capture_budget = phys.SoftDeferCaptureBudget;
  n.frame_budget_ms = phys.FrameBudgetMs;
  n.capture_over_budget = phys.CaptureOverBudget;
  n.heal_deferred_for_miss = phys.HealDeferredForMiss;
  n.stage_skip_remesh_pending_light = phys.StageSkipRemeshPendingLight;
  n.softdefer_empty_placeholder_n = phys.SoftDeferEmptyPlaceholderN;
  n.softdefer_empty_stuck_n = phys.SoftDeferEmptyStuckN;
  n.softdefer_empty_stuck_cx = phys.SoftDeferEmptyStuckCx;
  n.softdefer_empty_stuck_cy = phys.SoftDeferEmptyStuckCy;
  n.softdefer_empty_stuck_cz = phys.SoftDeferEmptyStuckCz;
  n.softdefer_empty_stuck_horiz = phys.SoftDeferEmptyStuckHoriz;
  n.softdefer_empty_age_max_frames = phys.SoftDeferEmptyAgeMaxFrames;
  n.softdefer_empty_owned_n = phys.SoftDeferEmptyOwnedN;
  n.softdefer_empty_publish_avoided = phys.SoftDeferEmptyPublishAvoided;
  n.softdefer_held_n = phys.SoftDeferHeldN;
  n.pending_cols = phys.PendingFocusCols;
  ++s.FramesSinceMemSample;
  if (s.FramesSinceMemSample >= 30)
  {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                             sizeof(pmc)))
    {
      s.LastRssMb =
          static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
      s.LastPrivateMb =
          static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
    }
#endif
    s.FramesSinceMemSample = 0;
  }
  n.rss_mb = s.LastRssMb;
  n.private_mb = s.LastPrivateMb;
  n.chunk_count = static_cast<int>(
      world.GetBlockWorld().GetChunkManager().GetResidentChunkCount());
  n.greedy_vertices = world.GetRenderInstanceCount();
  n.mesh_completed_n = phys.MeshCompletedN;
  n.mesh_completed_cap = phys.MeshCompletedCap;
  n.mesh_completed_discarded = phys.MeshCompletedDiscarded;
  n.relight_completed_n = phys.RelightCompletedN;
  n.relight_completed_cap = phys.RelightCompletedCap;
  n.relight_completed_discarded = phys.RelightCompletedDiscarded;
  n.relight_capture_col_horiz = phys.RelightCaptureColHoriz;
  n.relight_capture_finalize = phys.RelightCaptureFinalize;
  n.relight_capture_band_cy_span = phys.RelightCaptureBandCySpan;
  n.relight_capture_full_n = phys.RelightCaptureFullN;
  n.relight_capture_neighbor_light_n = phys.RelightCaptureNeighborLightN;
  n.relight_witness_hold_n = phys.RelightWitnessHoldN;
  n.relight_apply_n = phys.RelightApplyN;
  n.relight_apply_partial_n = phys.RelightApplyPartialN;
  n.relight_apply_final_n = phys.RelightApplyFinalN;
  n.relight_deferred_far_pending = phys.RelightDeferredFarPendingN;
  n.relight_deferred_far_enqueue_n = phys.RelightDeferredFarEnqueueN;
  n.mark_relit_skip_already_dirty_n = phys.MarkRelitSkipAlreadyDirtyN;
  n.mark_relit_skip_already_raa_n = phys.MarkRelitSkipAlreadyRaaN;
  n.mark_relit_skip_inflight_n = phys.MarkRelitSkipInflightN;
  n.mark_relit_skip_enter_lit_quiesce_n = phys.MarkRelitSkipEnterLitQuiesceN;
  n.mark_relit_schedule_n = phys.MarkRelitScheduleN;
  n.mark_relit_suppress_enter_settled_n = phys.MarkRelitSuppressEnterSettledN;
  n.sticky_insert_stale_after_apply_n = phys.StickyInsertStaleAfterApplyN;
  n.sticky_insert_seam_n = phys.StickyInsertSeamN;
  n.sticky_insert_other_n = phys.StickyInsertOtherN;
  n.sticky_erase_drawable_n = phys.StickyEraseDrawableN;
  n.sticky_erase_pending_clear_n = phys.StickyErasePendingClearN;
  n.sticky_erase_pruned_far_n = phys.StickyErasePrunedFarN;
  n.sticky_erase_remesh_commit_n = phys.StickyEraseRemeshCommitN;
  n.sticky_erase_other_n = phys.StickyEraseOtherN;
  n.dirty_n = phys.DirtyN;
  n.pending_light_n = phys.PendingLightN;
  n.relight_fifo_n = phys.RelightFifoN;
  n.dirty_dropped = phys.DirtyDropped;
  n.pending_light_dropped = phys.PendingLightDropped;
  n.relight_fifo_dropped = phys.RelightFifoDropped;
  n.relight_false_clear_n = phys.RelightFalseClearN;
  n.gpu_pool_used_mb = phys.GpuPoolUsedMb;
  n.gpu_pool_cap_mb = phys.GpuPoolCapMb;
  n.gpu_draw_cmds = phys.GpuDrawCmds;
  n.gpu_cull_ms = phys.GpuCullMs;
  n.gpu_cull_cpu_ms = phys.GpuCullMs;
  n.gpu_cull_gpu_ms = phys.GpuCullGpuMs;
  n.vertex_pool_fill = phys.VertexPoolFill;
  n.gpu_cull_indirect = phys.GpuCullIndirect;
  n.opaque_cmd_total = phys.OpaqueCmdTotal;
  n.opaque_cmd_on = phys.OpaqueCmdOn;
  n.opaque_gpu_packed_n = phys.OpaqueGpuPackedN;
  n.opaque_draw_n = phys.OpaqueDrawN;
  n.opaque_refs_cpu_vis = phys.OpaqueRefsCpuVis;
  n.opaque_refs_render_ready = phys.OpaqueRefsRenderReady;
  n.opaque_mdi_eligible = phys.OpaqueMdiEligible;
  n.cross_batch_count = phys.CrossBatchCount;
  n.cpu_aabb_would_on = phys.CpuAabbWouldOn;
  n.edit_immediate_n = phys.EditImmediateN;
  n.edit_dirty_n = phys.EditDirtyN;
  n.edit_neighbor_pending_frames = phys.EditNeighborPendingFrames;
  n.pool_unsync_uploads = phys.PoolUnsyncUploads;
  n.pool_fence_wait_ms = phys.PoolFenceWaitMs;
  n.chunk_meshed_culled0 = phys.ChunkMeshedCulled0;
  n.chunk_meshed_unlit = phys.ChunkMeshedUnlit;
  n.chunk_meshed_unlit_hidden = phys.ChunkMeshedUnlitHidden;
  n.chunk_meshed_unlit_preview = phys.ChunkMeshedUnlitPreview;
  n.chunk_not_ready = phys.ChunkNotReady;
  n.dark_face_near_n = phys.DarkFaceNearN;
  n.dark_face_stale_near_n = phys.DarkFaceStaleNearN;
  n.dark_face_void_near_n = phys.DarkFaceVoidNearN;
  n.dark_face_bx = phys.DarkFaceBlockX;
  n.dark_face_by = phys.DarkFaceBlockY;
  n.dark_face_bz = phys.DarkFaceBlockZ;
  n.dark_face_cx = phys.DarkFaceChunkX;
  n.dark_face_cy = phys.DarkFaceChunkY;
  n.dark_face_cz = phys.DarkFaceChunkZ;
  n.dark_face_block_id = phys.DarkFaceBlockId;
  n.dark_face_index = phys.DarkFaceIndex;
  n.dark_face_dist = phys.DarkFaceDist;
  n.gpu_mesh_vbo_dispatch = UGpuGreedyMesher::ConsumeMeshVboDispatchCount();
  n.gpu_light_seed_apply = ConsumeGpuSkylightSeedApplyCount();
  n.gpu_mask_readback = UGpuGreedyMesher::ConsumeMaskReadbackCount();
  n.gpu_transparent_sort_readback = ConsumeGpuTransparentSortReadbackCount();
  n.gpu_cull_stats_readback = ConsumeGpuCullStatsReadbackCount();
  n.gpu_blocklight_flood = ConsumeGpuBlocklightFloodCount();
  n.gpu_fluid_readback = ConsumeGpuFluidReadbackCount();
  n.gpu_light_readback = ConsumeGpuSkylightSeedReadbackCount();
  n.gpu_opaque_emit_gpu = ConsumeGpuOpaqueEmitCount();
  n.gpu_transparent_sort_gpu = ConsumeGpuTransparentSortCount();
  n.gpu_fallback = ConsumeGpuHotPathFallbackCount();
  n.gpu_fluid_scan_on = phys.GpuFluidScanOn;
  n.backend_mesher = phys.BackendMesher;
  n.backend_store = phys.BackendStore;
  n.backend_cull = phys.BackendCull;
  n.backend_fluid = phys.BackendFluid;
  n.backend_lighting_mode = phys.BackendLightingMode;
  n.caps_has_compute = phys.CapsHasCompute;
  n.caps_has_ssbo = phys.CapsHasSsbo;
  n.caps_probe_completed = phys.CapsProbeCompleted;
  n.android_gpu_user_pref = phys.AndroidGpuUserPref;
  n.android_gpu_effective = phys.AndroidGpuEffective;
  n.android_gpu_deny_reason = phys.AndroidGpuDenyReason;
  n.gl_version = phys.GlVersion;
  n.gl_renderer = phys.GlRenderer;
  n.keep_margin_eff = phys.KeepMarginEff;
  n.buffer_expand_events = phys.BufferExpandEvents;
  {
    const auto &tune = URuntimeTuning::Get();
    if (n.private_mb >= static_cast<double>(tune.MemoryBudgetMb))
    {
      n.memory_pressure = 2;
    }
    else if (n.private_mb >= static_cast<double>(tune.MemorySoftMb))
    {
      n.memory_pressure = 1;
    }
    else
    {
      n.memory_pressure = 0;
    }
    // Prefer controller-written pressure when present later.
    if (phys.MemoryPressure > n.memory_pressure)
    {
      n.memory_pressure = phys.MemoryPressure;
    }
  }
  return n;
}

void WriteJsonl(Session &s, const FrameNumbers &n, const char *kind,
                bool flush)
{
  if (!s.Jsonl.is_open())
  {
    return;
  }
  s.Jsonl << "{\"kind\":\"" << kind << "\""
          << ",\"wall_ms\":" << n.wall_ms << ",\"sim_ms\":" << n.sim_ms
          << ",\"swap_wait_ms\":" << n.swap_wait_ms
          << ",\"unaccounted_ms\":" << n.unaccounted_ms
          << ",\"input_ms\":" << n.input_ms
          << ",\"app_update_ms\":" << n.app_update_ms
          << ",\"world_extra_ms\":" << n.world_extra_ms
          << ",\"views_ms\":" << n.views_ms
          << ",\"do_movement_ms\":" << n.do_movement_ms
          << ",\"ensure_collision_ms\":" << n.ensure_collision_ms
          << ",\"creature_tick_ms\":" << n.creature_tick_ms
          << ",\"camera_move_ms\":" << n.camera_move_ms
          << ",\"camera_ground_support_ms\":" << n.camera_ground_support_ms
          << ",\"camera_locomotion_ms\":" << n.camera_locomotion_ms
          << ",\"camera_horiz_move_ms\":" << n.camera_horiz_move_ms
          << ",\"camera_sync_ms\":" << n.camera_sync_ms
          << ",\"environment_tick_ms\":" << n.environment_tick_ms
          << ",\"npc_intent_ms\":" << n.npc_intent_ms
          << ",\"controlled_influence_ms\":" << n.controlled_influence_ms
          << ",\"vitals_tick_ms\":" << n.vitals_tick_ms
          << ",\"status_effects_tick_ms\":" << n.status_effects_tick_ms
          << ",\"creatures_total\":" << n.creatures_total
          << ",\"creatures_ai_ticked\":" << n.creatures_ai_ticked
          << ",\"world_creatures_skipped\":" << n.world_creatures_skipped
          << ",\"player_locomotion_block_ms\":" << n.player_locomotion_block_ms
          << ",\"world_ai_after_player_ms\":" << n.world_ai_after_player_ms
          << ",\"creatures_ai_budget\":" << n.creatures_ai_budget
          << ",\"creatures_ai_deferred\":" << n.creatures_ai_deferred
          << ",\"stream_speed_clamp_scale\":" << n.stream_speed_clamp_scale
          << ",\"world_streaming_phase_ms\":" << n.world_streaming_phase_ms
          << ",\"block_input_ms\":" << n.block_input_ms
          << ",\"tick_env_ms\":" << n.tick_env_ms
          << ",\"physics_block_ms\":" << n.physics_block_ms
          << ",\"physics_drain_ms\":" << n.physics_drain_ms
          << ",\"physics_movement_ms\":" << n.physics_movement_ms
          << ",\"physics_substeps\":" << n.physics_substeps
          << ",\"break_complete_n\":" << n.break_complete_n
          << ",\"break_inflight_race_n\":" << n.break_inflight_race_n
          << ",\"break_dark_face_n\":" << n.break_dark_face_n
          << ",\"place_complete_n\":" << n.place_complete_n
          << ",\"place_emission_n\":" << n.place_emission_n
          << ",\"edit_light_emission\":" << n.edit_light_emission
          << ",\"edit_to_first_mesh_ms\":" << n.edit_to_first_mesh_ms
          << ",\"fast_relight_ms\":" << n.fast_relight_ms
          << ",\"prepare_frame_ms\":" << n.prepare_frame_ms
          << ",\"post_scene_ms\":" << n.post_scene_ms
          << ",\"gui_overlay_ms\":" << n.gui_overlay_ms
          << ",\"autosave_ms\":" << n.autosave_ms
          << ",\"autosave_deferred_n\":" << n.autosave_deferred_n
          << ",\"autosave_skipped_tick_n\":" << n.autosave_skipped_tick_n
          << ",\"dig_seam_pending_n\":" << n.dig_seam_pending_n
          << ",\"dig_seam_remesh_n\":" << n.dig_seam_remesh_n
          << ",\"stale_repair_wave_n\":" << n.stale_repair_wave_n
          << ",\"stand_rim_dirty_n\":" << n.stand_rim_dirty_n
          << ",\"stand_rim_imm_n\":" << n.stand_rim_imm_n
          << ",\"render_total_ms\":" << n.render_total_ms
          << ",\"residual_ms\":" << n.residual_ms
          << ",\"perf_collect_ms\":" << n.perf_collect_ms
          << ",\"perf_emit_ms\":" << n.perf_emit_ms
          << ",\"fluid_map_cpu_ms\":" << n.fluid_map_cpu_ms
          << ",\"fluid_map_gpu_ms\":" << n.fluid_map_gpu_ms
          << ",\"fluid_map_dirty\":" << n.fluid_map_dirty
          << ",\"fluid_map_full_rebuild\":" << n.fluid_map_full_rebuild
          << ",\"commit_apply_ms\":" << n.commit_apply_ms
          << ",\"commit_seal_ms\":" << n.commit_seal_ms
          << ",\"commit_physics_ms\":" << n.commit_physics_ms
          << ",\"streamer_update_ms\":" << n.streamer_update_ms
          << ",\"async_io_ms\":" << n.async_io_ms
          << ",\"relight_drain_ms\":" << n.relight_drain_ms
          << ",\"relight_capture_ms\":" << n.relight_capture_ms
          << ",\"relight_apply_ms\":" << n.relight_apply_ms
          << ",\"relight_fifo_drop_n\":" << n.relight_fifo_drop_n
          << ",\"relight_fifo_pin_saved_n\":" << n.relight_fifo_pin_saved_n
          << ",\"mesh_sync_ms\":" << n.mesh_sync_ms
          << ",\"mesh_snapshot_ms\":" << n.mesh_snapshot_ms
          << ",\"mesh_immediate_ms\":" << n.mesh_immediate_ms
          << ",\"mesh_immediate_count\":" << n.mesh_immediate_count
          << ",\"mesh_dirty_tick_ms\":" << n.mesh_dirty_tick_ms
          << ",\"mesh_dirty_prune_ms\":" << n.mesh_dirty_prune_ms
          << ",\"mesh_dirty_prune_n\":" << n.mesh_dirty_prune_n
          << ",\"mesh_dirty_sort_ms\":" << n.mesh_dirty_sort_ms
          << ",\"mesh_dirty_drain_ms\":" << n.mesh_dirty_drain_ms
          << ",\"mesh_dirty_drain_n\":" << n.mesh_dirty_drain_n
          << ",\"mesh_dirty_schedule_ms\":" << n.mesh_dirty_schedule_ms
          << ",\"mesh_dirty_schedule_ok_n\":" << n.mesh_dirty_schedule_ok_n
          << ",\"mesh_dirty_schedule_skip_n\":" << n.mesh_dirty_schedule_skip_n
          << ",\"mesh_dirty_gpu_ms\":" << n.mesh_dirty_gpu_ms
          << ",\"mesh_dirty_gpu_n\":" << n.mesh_dirty_gpu_n
          << ",\"mesh_dirty_sync_ms\":" << n.mesh_dirty_sync_ms
          << ",\"mesh_dirty_sync_n\":" << n.mesh_dirty_sync_n
          << ",\"dirty_touch_n\":" << n.dirty_touch_n
          << ",\"dirty_revisit_same_n\":" << n.dirty_revisit_same_n
          << ",\"dirty_fm_n\":" << n.dirty_fm_n
          << ",\"dirty_remesh_n\":" << n.dirty_remesh_n
          << ",\"prep_unfinished_calls_n\":" << n.prep_unfinished_calls_n
          << ",\"prep_unfinished_full_n\":" << n.prep_unfinished_full_n
          << ",\"prep_unfinished_incremental_n\":"
          << n.prep_unfinished_incremental_n
          << ",\"unfinished_cache_hit_n\":" << n.unfinished_cache_hit_n
          << ",\"unfinished_cache_overflow_n\":" << n.unfinished_cache_overflow_n
          << ",\"dirty_admit_budget_end\":" << n.dirty_admit_budget_end
          << ",\"first_mesh_schedule_cap\":" << n.first_mesh_schedule_cap
          << ",\"remesh_schedule_cap\":" << n.remesh_schedule_cap
          << ",\"relight_trim_far_n\":" << n.relight_trim_far_n
          << ",\"player_x\":" << n.player_x << ",\"player_y\":" << n.player_y
          << ",\"player_z\":" << n.player_z
          << ",\"phase_budget_over\":" << n.phase_budget_over
          << ",\"phase_miss_carve_out\":" << n.phase_miss_carve_out
          << ",\"miss_reserved_ms\":" << n.miss_reserved_ms
          << ",\"emerge_budget_ms\":" << n.emerge_budget_ms
          << ",\"render_preset\":" << n.render_preset
          << ",\"async_meshing\":" << n.async_meshing
          << ",\"mesh_emerge_prep_ms\":" << n.mesh_emerge_prep_ms
          << ",\"mesh_emerge_prep_missing_ms\":" << n.mesh_emerge_prep_missing_ms
          << ",\"mesh_emerge_prep_unfinished_ms\":"
          << n.mesh_emerge_prep_unfinished_ms
          << ",\"mesh_emerge_prep_sticky_ms\":" << n.mesh_emerge_prep_sticky_ms
          << ",\"mesh_emerge_prep_drop_dirty_ms\":"
          << n.mesh_emerge_prep_drop_dirty_ms
          << ",\"mesh_emerge_prep_other_ms\":" << n.mesh_emerge_prep_other_ms
          << ",\"prep_pending_light_ms\":" << n.prep_pending_light_ms
          << ",\"prep_black_sticky_ms\":" << n.prep_black_sticky_ms
          << ",\"prep_dirty_count_ms\":" << n.prep_dirty_count_ms
          << ",\"prep_softdefer_setup_ms\":" << n.prep_softdefer_setup_ms
          << ",\"softdefer_empty_scan_ms\":" << n.softdefer_empty_scan_ms
          << ",\"softdefer_empty_own_ms\":" << n.softdefer_empty_own_ms
          << ",\"keep_cols\":" << n.keep_cols
          << ",\"visual_cols\":" << n.visual_cols
          << ",\"idle_prefetch_ms\":" << n.idle_prefetch_ms
          << ",\"prefetch_visual_ops\":" << n.prefetch_visual_ops
          << ",\"prefetch_keep_ops\":" << n.prefetch_keep_ops
          << ",\"gen_backlog_total\":" << n.gen_backlog_total
          << ",\"phys_ms\":" << n.phys_ms << ",\"stream_ms\":" << n.stream_ms
          << ",\"mesh_emerge_ms\":" << n.mesh_emerge_ms
          << ",\"scene_ms\":" << n.scene_ms
          << ",\"view_ms\":" << n.view_ms << ",\"flat_ms\":" << n.flat_ms
          << ",\"gen_q\":" << n.gen_q << ",\"mesh_async\":" << n.mesh_async
          << ",\"dirty\":" << n.dirty
          << ",\"stream_loads\":" << n.stream_loads
          << ",\"stream_async_queued\":" << n.stream_async_queued
          << ",\"stream_disk_complete_n\":" << n.stream_disk_complete_n
          << ",\"stream_gen_commit_n\":" << n.stream_gen_commit_n
          << ",\"frontier_pressure\":" << n.frontier_pressure
          << ",\"stream_ring_blocked\":" << n.stream_ring_blocked
          << ",\"stream_near_skipped\":" << n.stream_near_skipped
          << ",\"stream_load_candidates\":" << n.stream_load_candidates
          << ",\"allow_proc_fill\":" << n.allow_proc_fill
          << ",\"column_absent_in_rd_n\":" << n.column_absent_in_rd_n
          << ",\"column_loaded_no_mesh_n\":" << n.column_loaded_no_mesh_n
          << ",\"column_bump_denied\":" << n.column_bump_denied
          << ",\"column_flow_upgrade_n\":" << n.column_flow_upgrade_n
          << ",\"column_lighting_n\":" << n.column_lighting_n
          << ",\"column_meshing_n\":" << n.column_meshing_n
          << ",\"column_render_ready_n\":" << n.column_render_ready_n
          << ",\"pending_light\":" << n.pending_light
          << ",\"stream_pressure\":" << n.stream_pressure
          << ",\"pending_light_focus\":" << n.pending_light_focus
          << ",\"focus_cx\":" << n.focus_cx << ",\"focus_cz\":" << n.focus_cz
          << ",\"underfeet_need\":" << n.underfeet_need
          << ",\"underfeet_draw_ok\":" << n.underfeet_draw_ok
          << ",\"underfeet_has_mesh\":" << n.underfeet_has_mesh
          << ",\"underfeet_sticky\":" << n.underfeet_sticky
          << ",\"underfeet_pending_light\":" << n.underfeet_pending_light
          << ",\"underfeet_reason\":" << n.underfeet_reason
          << ",\"underfeet_stage\":" << n.underfeet_stage
          << ",\"underfeet_opaque_present\":" << n.underfeet_opaque_present
          << ",\"underfeet_opaque_present_raw\":" << n.underfeet_opaque_present_raw
          << ",\"underfeet_opaque_present_predicted\":"
          << n.underfeet_opaque_present_predicted
          << ",\"lighting_relight_deferred\":" << n.lighting_relight_deferred
          << ",\"fog_pull_in_rd\":" << n.fog_pull_in_rd
          << ",\"fog_pull_in_margin\":" << n.fog_pull_in_margin
          << ",\"fog_pull_in_start_ratio\":" << n.fog_pull_in_start_ratio
          << ",\"fog_hole_debt\":" << n.fog_hole_debt
          << ",\"near_focus_holes\":" << n.near_focus_holes
          << ",\"visual_holes\":" << n.visual_holes
          << ",\"unfinished_visual\":" << n.unfinished_visual
          << ",\"light_debt\":" << n.light_debt
          << ",\"focus_missing_mesh\":" << n.focus_missing_mesh
          << ",\"miss_cx\":" << n.miss_cx
          << ",\"miss_cy\":" << n.miss_cy
          << ",\"miss_cz\":" << n.miss_cz
          << ",\"miss_horiz\":" << n.miss_horiz
          << ",\"focus_dark_mesh\":" << n.focus_dark_mesh
          << ",\"focus_pending_dark\":" << n.focus_pending_dark
          << ",\"focus_sticky_remesh\":" << n.focus_sticky_remesh
          << ",\"visible_black_focus_n\":" << n.visible_black_focus_n
          << ",\"visible_black_no_ticket_n\":" << n.visible_black_no_ticket_n
          << ",\"visible_black_progress_n\":" << n.visible_black_progress_n
          << ",\"visible_black_stalled_n\":" << n.visible_black_stalled_n
          << ",\"focus_not_render_ready\":" << n.focus_not_render_ready
          << ",\"focus_pressure\":" << n.focus_pressure
          << ",\"focus_dirty_chunks\":" << n.focus_dirty_chunks
          << ",\"focus_unfinished_ahead\":" << n.focus_unfinished_ahead
          << ",\"focus_unfinished_behind\":" << n.focus_unfinished_behind
          << ",\"mesh_discarded_late\":" << n.mesh_discarded_late
          << ",\"mesh_apply_stale\":" << n.mesh_apply_stale
          << ",\"mesh_apply_stale_delta\":" << n.mesh_apply_stale_delta
          << ",\"mesh_replace_hole_avoided\":" << n.mesh_replace_hole_avoided
          << ",\"pending_gpu_applies_n\":" << n.pending_gpu_applies_n
          << ",\"pending_gpu_queued_n\":" << n.pending_gpu_queued_n
          << ",\"pending_gpu_kicked_n\":" << n.pending_gpu_kicked_n
          << ",\"gpu_kick_n\":" << n.gpu_kick_n
          << ",\"gpu_finish_n\":" << n.gpu_finish_n
          << ",\"gpu_finish_not_ready_n\":" << n.gpu_finish_not_ready_n
          << ",\"mesh_schedule_final\":" << n.mesh_schedule_final
          << ",\"mesh_drain_final\":" << n.mesh_drain_final
          << ",\"mesh_admission_mode\":" << n.mesh_admission_mode
          << ",\"post_load_ring_not_ready\":" << n.post_load_ring_not_ready
          << ",\"enter_game_warmup_missing_greedy\":"
          << n.enter_game_warmup_missing_greedy
          << ",\"softdefer_capture_floor_hits\":"
          << n.softdefer_capture_floor_hits
          << ",\"softdefer_capture_floor_hits_delta\":"
          << n.softdefer_capture_floor_hits_delta
          << ",\"softdefer_witness_retarget\":" << n.softdefer_witness_retarget
          << ",\"softdefer_witness_retarget_delta\":"
          << n.softdefer_witness_retarget_delta
          << ",\"softdefer_witness_horiz\":" << n.softdefer_witness_horiz
          << ",\"softdefer_capture_budget\":" << n.softdefer_capture_budget
          << ",\"frame_budget_ms\":" << n.frame_budget_ms
          << ",\"capture_over_budget\":" << n.capture_over_budget
          << ",\"heal_deferred_for_miss\":" << n.heal_deferred_for_miss
          << ",\"stage_skip_remesh_pending_light\":"
          << n.stage_skip_remesh_pending_light
          << ",\"softdefer_empty_placeholder_n\":"
          << n.softdefer_empty_placeholder_n
          << ",\"softdefer_empty_stuck_n\":" << n.softdefer_empty_stuck_n
          << ",\"softdefer_empty_stuck_cx\":" << n.softdefer_empty_stuck_cx
          << ",\"softdefer_empty_stuck_cy\":" << n.softdefer_empty_stuck_cy
          << ",\"softdefer_empty_stuck_cz\":" << n.softdefer_empty_stuck_cz
          << ",\"softdefer_empty_stuck_horiz\":"
          << n.softdefer_empty_stuck_horiz
          << ",\"softdefer_empty_age_max_frames\":"
          << n.softdefer_empty_age_max_frames
          << ",\"softdefer_empty_owned_n\":" << n.softdefer_empty_owned_n
          << ",\"softdefer_empty_publish_avoided\":"
          << n.softdefer_empty_publish_avoided
          << ",\"softdefer_held_n\":" << n.softdefer_held_n
          << ",\"rss_mb\":" << n.rss_mb << ",\"private_mb\":" << n.private_mb
          << ",\"chunk_count\":" << n.chunk_count
          << ",\"greedy_vertices\":" << n.greedy_vertices
          << ",\"mesh_completed_n\":" << n.mesh_completed_n
          << ",\"mesh_completed_cap\":" << n.mesh_completed_cap
          << ",\"mesh_completed_discarded\":" << n.mesh_completed_discarded
          << ",\"mesh_completed_discarded_delta\":"
          << n.mesh_completed_discarded_delta
          << ",\"relight_completed_n\":" << n.relight_completed_n
          << ",\"relight_completed_cap\":" << n.relight_completed_cap
          << ",\"relight_completed_discarded\":" << n.relight_completed_discarded
          << ",\"relight_capture_col_horiz\":" << n.relight_capture_col_horiz
          << ",\"relight_capture_finalize\":" << n.relight_capture_finalize
          << ",\"relight_capture_band_cy_span\":" << n.relight_capture_band_cy_span
          << ",\"relight_capture_full_n\":" << n.relight_capture_full_n
          << ",\"relight_capture_neighbor_light_n\":"
          << n.relight_capture_neighbor_light_n
          << ",\"relight_witness_hold_n\":" << n.relight_witness_hold_n
          << ",\"relight_apply_n\":" << n.relight_apply_n
          << ",\"relight_apply_partial_n\":" << n.relight_apply_partial_n
          << ",\"relight_apply_final_n\":" << n.relight_apply_final_n
          << ",\"relight_deferred_far_pending\":" << n.relight_deferred_far_pending
          << ",\"relight_deferred_far_enqueue_n\":" << n.relight_deferred_far_enqueue_n
          << ",\"mark_relit_skip_already_dirty_n\":"
          << n.mark_relit_skip_already_dirty_n
          << ",\"mark_relit_skip_already_raa_n\":"
          << n.mark_relit_skip_already_raa_n
          << ",\"mark_relit_skip_inflight_n\":" << n.mark_relit_skip_inflight_n
          << ",\"mark_relit_skip_enter_lit_quiesce_n\":"
          << n.mark_relit_skip_enter_lit_quiesce_n
          << ",\"mark_relit_schedule_n\":" << n.mark_relit_schedule_n
          << ",\"mark_relit_suppress_enter_settled_n\":"
          << n.mark_relit_suppress_enter_settled_n
          << ",\"sticky_insert_stale_after_apply_n\":"
          << n.sticky_insert_stale_after_apply_n
          << ",\"sticky_insert_seam_n\":" << n.sticky_insert_seam_n
          << ",\"sticky_insert_other_n\":" << n.sticky_insert_other_n
          << ",\"sticky_erase_drawable_n\":" << n.sticky_erase_drawable_n
          << ",\"sticky_erase_pending_clear_n\":" << n.sticky_erase_pending_clear_n
          << ",\"sticky_erase_pruned_far_n\":" << n.sticky_erase_pruned_far_n
          << ",\"sticky_erase_remesh_commit_n\":" << n.sticky_erase_remesh_commit_n
          << ",\"sticky_erase_other_n\":" << n.sticky_erase_other_n
          << ",\"dirty_n\":" << n.dirty_n
          << ",\"pending_light_n\":" << n.pending_light_n
          << ",\"relight_fifo_n\":" << n.relight_fifo_n
          << ",\"dirty_dropped\":" << n.dirty_dropped
          << ",\"pending_light_dropped\":" << n.pending_light_dropped
          << ",\"relight_fifo_dropped\":" << n.relight_fifo_dropped
          << ",\"relight_false_clear_n\":" << n.relight_false_clear_n
          << ",\"gpu_pool_used_mb\":" << n.gpu_pool_used_mb
          << ",\"gpu_pool_cap_mb\":" << n.gpu_pool_cap_mb
          << ",\"gpu_draw_cmds\":" << n.gpu_draw_cmds
          << ",\"gpu_cull_ms\":" << n.gpu_cull_ms
          << ",\"gpu_cull_cpu_ms\":" << n.gpu_cull_cpu_ms
          << ",\"gpu_cull_gpu_ms\":" << n.gpu_cull_gpu_ms
          << ",\"vertex_pool_fill\":" << n.vertex_pool_fill
          << ",\"gpu_cull_indirect\":" << n.gpu_cull_indirect
          << ",\"opaque_cmd_total\":" << n.opaque_cmd_total
          << ",\"opaque_cmd_on\":" << n.opaque_cmd_on
          << ",\"opaque_gpu_packed_n\":" << n.opaque_gpu_packed_n
          << ",\"opaque_draw_n\":" << n.opaque_draw_n
          << ",\"opaque_refs_cpu_vis\":" << n.opaque_refs_cpu_vis
          << ",\"opaque_refs_render_ready\":" << n.opaque_refs_render_ready
          << ",\"opaque_mdi_eligible\":" << n.opaque_mdi_eligible
          << ",\"cross_batch_count\":" << n.cross_batch_count
          << ",\"cpu_aabb_would_on\":" << n.cpu_aabb_would_on
          << ",\"edit_immediate_n\":" << n.edit_immediate_n
          << ",\"edit_dirty_n\":" << n.edit_dirty_n
          << ",\"edit_neighbor_pending_frames\":"
          << n.edit_neighbor_pending_frames
          << ",\"pool_unsync_uploads\":" << n.pool_unsync_uploads
          << ",\"pool_fence_wait_ms\":" << n.pool_fence_wait_ms
          << ",\"chunk_meshed_culled0\":" << n.chunk_meshed_culled0
          << ",\"chunk_meshed_unlit\":" << n.chunk_meshed_unlit
          << ",\"chunk_meshed_unlit_hidden\":" << n.chunk_meshed_unlit_hidden
          << ",\"chunk_meshed_unlit_preview\":" << n.chunk_meshed_unlit_preview
          << ",\"chunk_not_ready\":" << n.chunk_not_ready
          << ",\"dark_face_near_n\":" << n.dark_face_near_n
          << ",\"dark_face_stale_near_n\":" << n.dark_face_stale_near_n
          << ",\"dark_face_void_near_n\":" << n.dark_face_void_near_n
          << ",\"dark_face_bx\":" << n.dark_face_bx
          << ",\"dark_face_by\":" << n.dark_face_by
          << ",\"dark_face_bz\":" << n.dark_face_bz
          << ",\"dark_face_cx\":" << n.dark_face_cx
          << ",\"dark_face_cy\":" << n.dark_face_cy
          << ",\"dark_face_cz\":" << n.dark_face_cz
          << ",\"dark_face_block_id\":" << n.dark_face_block_id
          << ",\"dark_face_index\":" << n.dark_face_index
          << ",\"dark_face_dist\":" << n.dark_face_dist
          << ",\"gpu_mesh_vbo_dispatch\":" << n.gpu_mesh_vbo_dispatch
          << ",\"gpu_light_seed_apply\":" << n.gpu_light_seed_apply
          << ",\"gpu_mask_readback\":" << n.gpu_mask_readback
          << ",\"gpu_transparent_sort_readback\":"
          << n.gpu_transparent_sort_readback
          << ",\"gpu_cull_stats_readback\":" << n.gpu_cull_stats_readback
          << ",\"gpu_blocklight_flood\":" << n.gpu_blocklight_flood
          << ",\"gpu_fluid_readback\":" << n.gpu_fluid_readback
          << ",\"gpu_light_readback\":" << n.gpu_light_readback
          << ",\"gpu_opaque_emit_gpu\":" << n.gpu_opaque_emit_gpu
          << ",\"gpu_transparent_sort_gpu\":" << n.gpu_transparent_sort_gpu
          << ",\"gpu_fallback\":" << n.gpu_fallback
          << ",\"gpu_fluid_scan_on\":" << n.gpu_fluid_scan_on
          << ",\"backend_mesher\":\"" << n.backend_mesher << "\""
          << ",\"backend_store\":\"" << n.backend_store << "\""
          << ",\"backend_cull\":\"" << n.backend_cull << "\""
          << ",\"backend_fluid\":\"" << n.backend_fluid << "\""
          << ",\"backend_lighting_mode\":\"" << n.backend_lighting_mode << "\""
          << ",\"caps_has_compute\":" << n.caps_has_compute
          << ",\"caps_has_ssbo\":" << n.caps_has_ssbo
          << ",\"caps_probe_completed\":" << n.caps_probe_completed
          << ",\"android_gpu_user_pref\":" << n.android_gpu_user_pref
          << ",\"android_gpu_effective\":" << n.android_gpu_effective
          << ",\"android_gpu_deny_reason\":\"" << n.android_gpu_deny_reason
          << "\""
          << ",\"gl_version\":\"" << n.gl_version << "\""
          << ",\"gl_renderer\":\"" << n.gl_renderer << "\""
          << ",\"memory_pressure\":" << n.memory_pressure
          << ",\"keep_margin_eff\":" << n.keep_margin_eff
          << ",\"buffer_expand_events\":" << n.buffer_expand_events
          << ",\"black_sticky\":" << n.focus_sticky_remesh
          << ",\"visible_black_focus_n\":" << n.visible_black_focus_n
          << ",\"visible_black_no_ticket_n\":" << n.visible_black_no_ticket_n
          << ",\"visible_black_progress_n\":" << n.visible_black_progress_n
          << ",\"visible_black_stalled_n\":" << n.visible_black_stalled_n
          << ",\"pending_cols\":\"" << n.pending_cols << "\""
          << ",\"max_wall_ms\":" << n.max_wall_ms
          << ",\"max_stream_ms\":" << n.max_stream_ms
          << ",\"max_mesh_emerge_ms\":" << n.max_mesh_emerge_ms
          << ",\"max_phys_ms\":" << n.max_phys_ms
          << ",\"max_dirty\":" << n.max_dirty
          << ",\"max_ring_blocked\":" << n.max_ring_blocked
          << ",\"max_pending_light\":" << n.max_pending_light
          << ",\"frames\":" << n.frames << ",\"spikes\":" << n.spikes << "}\n";
  if (flush)
  {
    s.Jsonl.flush();
  }
}

void LogLine(const FrameNumbers &n, const char *kind, int frames,
             double max_wall)
{
  std::ostringstream oss;
  oss << "[Perf] kind=" << kind << " wall_ms=" << n.wall_ms
      << " sim_ms=" << n.sim_ms << " swap_wait_ms=" << n.swap_wait_ms
      << " stream_ms=" << n.stream_ms << " phys_ms=" << n.phys_ms
      << " mesh_emerge_ms=" << n.mesh_emerge_ms << " scene_ms=" << n.scene_ms
      << " Dirty=" << n.dirty << " GenQ=" << n.gen_q
      << " MeshAsync=" << n.mesh_async << " ring_blocked=" << n.stream_ring_blocked
      << " near_skip=" << n.stream_near_skipped
      << " pending_light=" << n.pending_light
      << " stream_pressure=" << n.stream_pressure
      << " pending_light_focus=" << n.pending_light_focus
      << " focus=(" << n.focus_cx << "," << n.focus_cz << ")"
      << " underfeet=" << n.underfeet_need << " visual_holes=" << n.visual_holes
      << " unfinished=" << n.unfinished_visual
      << " not_render_ready=" << n.focus_not_render_ready
      << " pending_dark=" << n.focus_pending_dark
      << " sticky=" << n.focus_sticky_remesh
      << " holes=" << n.near_focus_holes;
  if (n.dark_face_near_n > 0)
  {
    oss << " dark_face_n=" << n.dark_face_near_n << " dark_block=("
        << n.dark_face_bx << "," << n.dark_face_by << "," << n.dark_face_bz
        << ") dark_chunk=(" << n.dark_face_cx << "," << n.dark_face_cy << ","
        << n.dark_face_cz << ") dark_id=" << n.dark_face_block_id
        << " dark_face=" << n.dark_face_index
        << " dark_dist=" << n.dark_face_dist;
  }
  if (!n.pending_cols.empty())
  {
    oss << " pending_cols=" << n.pending_cols;
  }
  oss << " max_wall_ms=" << max_wall << " max_stream_ms=" << n.max_stream_ms
      << " max_ring=" << n.max_ring_blocked << " frames=" << frames
      << " perf_collect_ms=" << n.perf_collect_ms
      << " perf_emit_ms=" << n.perf_emit_ms;
  LOG(INFO) << oss.str();
}

void Accumulate(Session &s, const FrameNumbers &n)
{
  s.AccumWallMs += n.wall_ms;
  s.AccumSimMs += n.sim_ms;
  s.AccumSwapMs += n.swap_wait_ms;
  s.AccumUnaccountedMs += n.unaccounted_ms;
  s.AccumInputMs += n.input_ms;
  s.AccumAppUpdateMs += n.app_update_ms;
  s.AccumWorldExtraMs += n.world_extra_ms;
  s.AccumPrepareMs += n.prepare_frame_ms;
  s.AccumPostSceneMs += n.post_scene_ms;
  s.AccumGuiMs += n.gui_overlay_ms;
  s.AccumResidualMs += n.residual_ms;
  s.AccumFluidCpuMs += n.fluid_map_cpu_ms;
  s.AccumFluidGpuMs += n.fluid_map_gpu_ms;
  s.AccumStreamMs += n.stream_ms;
  s.AccumMeshEmergeMs += n.mesh_emerge_ms;
  s.AccumWorldStreamingPhaseMs += n.world_streaming_phase_ms;
  s.AccumSceneMs += n.scene_ms;
  s.AccumPhysMs += n.phys_ms;
  s.AccumPerfCollectMs += n.perf_collect_ms;
  s.AccumPerfEmitMs += n.perf_emit_ms;
  s.MaxWallMs = (std::max)(s.MaxWallMs, n.wall_ms);
  s.MaxStreamMs = (std::max)(s.MaxStreamMs, n.stream_ms);
  s.MaxMeshEmergeMs = (std::max)(s.MaxMeshEmergeMs, n.mesh_emerge_ms);
  s.MaxPhysMs = (std::max)(s.MaxPhysMs, n.phys_ms);
  s.MaxDirty = (std::max)(s.MaxDirty, n.dirty);
  s.MaxRingBlocked = (std::max)(s.MaxRingBlocked, n.stream_ring_blocked);
  s.MaxPendingLight = (std::max)(s.MaxPendingLight, n.pending_light);
  ++s.FrameCount;
}

FrameNumbers AverageFromSession(Session &s, const FrameNumbers &last)
{
  FrameNumbers avg = last;
  const double inv = 1.0 / static_cast<double>(s.FrameCount);
  avg.wall_ms = s.AccumWallMs * inv;
  avg.sim_ms = s.AccumSimMs * inv;
  avg.swap_wait_ms = s.AccumSwapMs * inv;
  avg.unaccounted_ms = s.AccumUnaccountedMs * inv;
  avg.input_ms = s.AccumInputMs * inv;
  avg.app_update_ms = s.AccumAppUpdateMs * inv;
  avg.world_extra_ms = s.AccumWorldExtraMs * inv;
  avg.prepare_frame_ms = s.AccumPrepareMs * inv;
  avg.post_scene_ms = s.AccumPostSceneMs * inv;
  avg.gui_overlay_ms = s.AccumGuiMs * inv;
  avg.residual_ms = s.AccumResidualMs * inv;
  avg.fluid_map_cpu_ms = s.AccumFluidCpuMs * inv;
  avg.fluid_map_gpu_ms = s.AccumFluidGpuMs * inv;
  avg.stream_ms = s.AccumStreamMs * inv;
  avg.mesh_emerge_ms = s.AccumMeshEmergeMs * inv;
  avg.world_streaming_phase_ms = s.AccumWorldStreamingPhaseMs * inv;
  avg.scene_ms = s.AccumSceneMs * inv;
  avg.phys_ms = s.AccumPhysMs * inv;
  avg.perf_collect_ms = s.AccumPerfCollectMs * inv;
  avg.perf_emit_ms = s.AccumPerfEmitMs * inv;
  avg.max_wall_ms = s.MaxWallMs;
  avg.max_stream_ms = s.MaxStreamMs;
  avg.max_mesh_emerge_ms = s.MaxMeshEmergeMs;
  avg.max_phys_ms = s.MaxPhysMs;
  avg.max_dirty = s.MaxDirty;
  avg.max_ring_blocked = s.MaxRingBlocked;
  avg.max_pending_light = s.MaxPendingLight;
  avg.frames = s.FrameCount;
  avg.spikes = s.SpikesWrittenThisPeriod;
  return avg;
}

void ResetAccum(Session &s)
{
  s.AccumWallMs = 0.0;
  s.AccumSimMs = 0.0;
  s.AccumSwapMs = 0.0;
  s.AccumUnaccountedMs = 0.0;
  s.AccumInputMs = 0.0;
  s.AccumAppUpdateMs = 0.0;
  s.AccumWorldExtraMs = 0.0;
  s.AccumPrepareMs = 0.0;
  s.AccumPostSceneMs = 0.0;
  s.AccumGuiMs = 0.0;
  s.AccumResidualMs = 0.0;
  s.AccumFluidCpuMs = 0.0;
  s.AccumFluidGpuMs = 0.0;
  s.AccumStreamMs = 0.0;
  s.AccumMeshEmergeMs = 0.0;
  s.AccumWorldStreamingPhaseMs = 0.0;
  s.AccumSceneMs = 0.0;
  s.AccumPhysMs = 0.0;
  s.AccumPerfCollectMs = 0.0;
  s.AccumPerfEmitMs = 0.0;
  s.MaxWallMs = 0.0;
  s.MaxStreamMs = 0.0;
  s.MaxMeshEmergeMs = 0.0;
  s.MaxPhysMs = 0.0;
  s.MaxDirty = 0;
  s.MaxRingBlocked = 0;
  s.MaxPendingLight = 0;
  s.SpikesWrittenThisPeriod = 0;
  s.FrameCount = 0;
}

} // namespace

void UFramePerfMonitor::EnsureSession()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  OpenSessionLocked(s);
}

void UFramePerfMonitor::OnInGameFrame(UWorld &world, double swap_wait_ms,
                                      double interval_sec, double frame_wall_ms)
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  OpenSessionLocked(s);

  const auto collect_begin = std::chrono::steady_clock::now();
  FrameNumbers n = Compute(world, swap_wait_ms, frame_wall_ms, s);
  n.perf_collect_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - collect_begin)
          .count();
  Accumulate(s, n);

  // Cap spike disk writes: cheap in-memory accumulate always; at most a few
  // spike samples per period, without fflush (period flush covers durability).
  constexpr int kMaxSpikesPerPeriod = 6;
  if (n.wall_ms > 100.0 && s.SpikesWrittenThisPeriod < kMaxSpikesPerPeriod)
  {
    const auto emit_begin = std::chrono::steady_clock::now();
    WriteJsonl(s, n, "spike", /*flush=*/false);
    n.perf_emit_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - emit_begin)
            .count();
    s.AccumPerfEmitMs += n.perf_emit_ms;
    ++s.SpikesWrittenThisPeriod;
    if (n.wall_ms > 250.0)
    {
      LogLine(n, "spike", 1, n.wall_ms);
    }
  }

  if (n.underfeet_reason == 7 || (n.opaque_cmd_total > 0 && n.opaque_cmd_on == 0))
  {
    LOG(INFO) << "[Perf] underfeet_diag reason=" << n.underfeet_reason
              << " stage=" << n.underfeet_stage
              << " draw_ok=" << n.underfeet_draw_ok
              << " has_mesh=" << n.underfeet_has_mesh
              << " sticky=" << n.underfeet_sticky
              << " pending_light=" << n.underfeet_pending_light
              << " lighting_deferred=" << n.lighting_relight_deferred
              << " env_ms=" << n.environment_tick_ms
              << " npc_ms=" << n.npc_intent_ms
              << " infl_ms=" << n.controlled_influence_ms
              << " vitals_ms=" << n.vitals_tick_ms
              << " creatures=" << n.creatures_total
              << "/" << n.creatures_ai_ticked
              << " world_skip=" << n.world_creatures_skipped
              << " clamp=" << n.stream_speed_clamp_scale
              << " opaque_present=" << n.underfeet_opaque_present
              << " opaque_cmd_total=" << n.opaque_cmd_total
              << " opaque_cmd_on=" << n.opaque_cmd_on
              << " opaque_packed=" << n.opaque_gpu_packed_n
              << " opaque_draw=" << n.opaque_draw_n
              << " focus=(" << n.focus_cx << "," << n.focus_cz << ")"
              << " player=(" << n.player_x << "," << n.player_y << ","
              << n.player_z << ")"
              << " chunk_count=" << n.chunk_count
              << " gpu_pool_mb=" << n.gpu_pool_used_mb;
  }

  const double interval = interval_sec > 0.05 ? interval_sec : 2.0;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed =
      std::chrono::duration<double>(now - s.LastEmit).count();
  if (elapsed < interval || s.FrameCount <= 0)
  {
    return;
  }

  // Force a memory sample on period boundaries for MemoryBudget accuracy.
  s.FramesSinceMemSample = 30;

  const FrameNumbers avg = AverageFromSession(s, n);
  FrameNumbers period = avg;
  period.mesh_apply_stale_delta =
      n.mesh_apply_stale >= s.MeshApplyStaleAtPeriodStart
          ? n.mesh_apply_stale - s.MeshApplyStaleAtPeriodStart
          : 0;
  period.mesh_completed_discarded_delta =
      n.mesh_completed_discarded >= s.MeshCompletedDiscardedAtPeriodStart
          ? n.mesh_completed_discarded -
                s.MeshCompletedDiscardedAtPeriodStart
          : 0;
  period.softdefer_capture_floor_hits_delta =
      n.softdefer_capture_floor_hits >= s.SoftDeferCaptureFloorHitsAtPeriodStart
          ? n.softdefer_capture_floor_hits -
                s.SoftDeferCaptureFloorHitsAtPeriodStart
          : 0;
  period.softdefer_witness_retarget_delta =
      n.softdefer_witness_retarget >= s.SoftDeferWitnessRetargetAtPeriodStart
          ? n.softdefer_witness_retarget -
                s.SoftDeferWitnessRetargetAtPeriodStart
          : 0;
  const auto emit_begin = std::chrono::steady_clock::now();
  WriteJsonl(s, period, "period", /*flush=*/true);
  period.perf_emit_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - emit_begin)
          .count();
  LogLine(period, "period", s.FrameCount, s.MaxWallMs);
  s.MeshApplyStaleAtPeriodStart = n.mesh_apply_stale;
  s.MeshCompletedDiscardedAtPeriodStart = n.mesh_completed_discarded;
  s.SoftDeferCaptureFloorHitsAtPeriodStart = n.softdefer_capture_floor_hits;
  s.SoftDeferWitnessRetargetAtPeriodStart = n.softdefer_witness_retarget;
  // CullStats SubData only when ShowPerformance enables readback — not every
  // period (GPU sync hitch ~2s on cruise).
  ResetAccum(s);
  s.LastEmit = now;
}

void UFramePerfMonitor::Shutdown()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  if (s.FrameCount > 0 && s.Jsonl.is_open())
  {
    FrameNumbers last{};
    const FrameNumbers avg = AverageFromSession(s, last);
    WriteJsonl(s, avg, "shutdown", /*flush=*/true);
    LogLine(avg, "shutdown", s.FrameCount, s.MaxWallMs);
    ResetAccum(s);
  }
  if (s.Jsonl.is_open())
  {
    s.Jsonl.close();
  }
  s.Opened = false;
}

std::string UFramePerfMonitor::GetLastSessionPath()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  return s.Path;
}

double UFramePerfMonitor::GetLastPrivateMb()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  return s.LastPrivateMb;
}

double UFramePerfMonitor::GetLastRssMb()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  return s.LastRssMb;
}

} // namespace cutum
