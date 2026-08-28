#pragma once

#include <cstdint>
#include <string>

namespace cutum
{

class UWorld;

struct EnterLitSample
{
  double elapsed_ms{0.0};
  int snapshot_debt{0};
  int snapshot_size{0};
  int pending_global{0};
  int fifo_n{0};
  int inflight{0};
  int chunk_resident{0};
  bool streaming_frozen{false};
  bool mesh_dirty{false};
  bool mesh_missing_greedy{false};
  int mesh_gpu_pending_near{0};
  bool mesh_async_pending{false};
  bool mesh_visual_warmup{false};
  /// Era44: visual ring debt (CountPostLoadRingNotReady).
  int ring_not_ready{0};
  int relight_completed_n{0};
  uint64_t stage_skip_remesh_pending_light{0};
  int relight_fifo_dropped{0};
  int top_dirty_cx{0};
  int top_dirty_cz{0};
  /// SRBR-P0.2: gate miss SoT (spawn ring presentable band).
  int gate_miss_cx{0};
  int gate_miss_cy{0};
  int gate_miss_cz{0};
  int gate_miss_soft_held{0};
  int gate_miss_defer{0};
  int gate_miss_inflight{0};
  int gate_miss_has_greedy{0};
  int gate_miss_drawable{0};
  int gate_miss_gpu_resident{0};
  int gate_miss_gpu_quad{0};
  /// Era45: R4 diagnostics.
  int remesh_after_apply_n{0};
  int stuck_dirty_cx{0};
  int stuck_dirty_cy{0};
  int stuck_dirty_cz{0};
  bool suppress_relight_seam{false};
  uint64_t mark_relit_raa_total{0};
  /// Era46: ring blocker label (dirty|gpu|async|missing|none).
  const char *ring_blocker{"none"};
  uint64_t raa_commit_mark_dirty_n{0};
  uint64_t markdirty_to_raa_n{0};
  /// Era47 P0: producer/egress forensics.
  int gpu_kick_n{0};
  int gpu_finish_n{0};
  uint64_t mark_relit_prefer_kick_n{0};
  uint64_t dirty_schedule_skip_inflight_n{0};
  int pending_gpu_global{0};
  /// Era47: latch + Dirty size (prove prune path).
  bool enter_lit_quiesce{false};
  int dirty_n{0};
  int stuck_has_chunk{0};
  int stuck_has_drawable{0};
  /// Era48: full-RD visibility debt (missing/FullyDark/pending light).
  int visibility_debt{0};
  int dark_face_near_n{0};
  int dark_face_void_near_n{0};
  /// Era52: enter gate terminal / done forensics.
  int enter_terminal_held_n{0};
  int gate_done_n{0};
  int enter_phantom_dirty_pruned_n{0};
  /// Enter SoT: underfeet present (opaque draw), not underfeet_need.
  int underfeet_present_ready{0};
  int spawn_mesh_ring_ready{0};
};

/// Era44: per-frame step timings (deltas) inside gpu_warmup.
struct EnterWarmupStepSample
{
  double drain_mesh_ms{0.0};
  double gate_drain_ms{0.0};
  double lit_pass_ms{0.0};
  double relight_drain_ms{0.0};
  double mesh_emerge_ms{0.0};
  double mesh_immediate_ms{0.0};
  double mesh_emerge_prep_missing_ms{0.0};
  double mesh_emerge_prep_sticky_ms{0.0};
  double mesh_emerge_prep_drop_dirty_ms{0.0};
  int relight_completed_delta{0};
  int gpu_finish_delta{0};
};

class UEnterLitDiagnostics
{
public:
  static void BeginSession();
  static void EndSession();
  static void Sample(UWorld &world, double elapsed_ms, EnterLitSample &out);
  static void MaybeLog(const EnterLitSample &sample, int frame_index,
                       int every_n_frames = 30);
  /// Heartbeat by elapsed_ms (default every 2s) for long gpu_warmup stalls.
  static void MaybeLogHeartbeat(const EnterLitSample &sample,
                                double heartbeat_ms = 2000.0);
  /// Era44: accumulate per-step timings for profile summary.
  static void RecordFrameSteps(const EnterWarmupStepSample &steps);
  /// Era44: emit [EnterWarmup] profile when ring becomes ready.
  static void MaybeLogProfileSummary(const EnterLitSample &sample);
};

} // namespace cutum
