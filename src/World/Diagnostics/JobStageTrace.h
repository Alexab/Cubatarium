#ifndef JOB_STAGE_TRACE_H
#define JOB_STAGE_TRACE_H

#include <cstdint>
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
  uint8_t queue_reason{0};
  double created_ms{0.0};
  double stage_ms{0.0};
  /// Milliseconds from mesh-job creation to this event, when available.
  double elapsed_ms{0.0};
};

/// Bounded, opt-in sample of a chunk counted by the visible-black census.
/// Captures both column-level ownership flags and the exact slice's mesh/demand
/// stamps so aggregate census counts can be traced to their real work owner.
struct VisualBlackTraceRecord
{
  /// 0=black-attribution, 1=focus slice, 2=renderer draw-gate candidate,
  /// 3=visible relight-queue admission, 4=visible remesh scheduling attempt.
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
  uint64_t demand_published_geom_rev{0};
  uint64_t demand_published_light_rev{0};
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
  uint8_t draw_gate_ready{0};
  uint8_t stale_face_index{0};
  uint8_t stale_sample_light{0};
  uint8_t stale_sample_gpu_path{0};
  /// sample_kind=2: 1=CPU opaque, 2=CPU transparent, 3=packed opaque,
  /// 4=packed transparent.
  uint8_t renderer_path{0};
  uint32_t renderer_cpu_index_count{0};
  uint32_t renderer_gpu_quad_count{0};
  /// sample_kind=2 flags 0..17: drawable, satisfying, live GPU, fully dark,
  /// lit drawable, stale dark, dirty, mesh in-flight, GPU pending, extract
  /// in-flight, queued/kicked GPU apply, pending light, async relight, sticky
  /// remesh, repair progress, and column draw-ready/repair-ticket.
  uint32_t renderer_gate_flags{0};
  uint8_t renderer_column_reason{0};
  uint8_t renderer_column_draw_ok{0};
  uint8_t renderer_column_has_repair_ticket{0};
  /// Focus sample's persistence relight queue location/band at capture time.
  /// 0=not keyed, 1=priority deque, 2=far deque, 3=keyed but absent from deque.
  uint8_t relight_queue_kind{0};
  uint8_t relight_y_band_defined{0};
  int32_t relight_queue_index{-1};
  int32_t relight_queue_size{0};
  int32_t relight_band_min_y{0};
  int32_t relight_band_max_y{-1};
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
  static constexpr size_t kCullDecisionRingCapacity = 64;
  static constexpr size_t kVisualBlackTraceRingCapacity = 1024;

  static void Note(const JobStageSpan &span);
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
  static void ForEachVisualBlackNewest(
      size_t max_n, void (*fn)(const VisualBlackTraceRecord &, void *),
      void *ctx);
  static const char *StageName(JobStage s);
  /// A36 S1: note cull exclusion for a tracked chunk (bounded ring).
  static void NoteCullDecision(int32_t cx, int32_t cy, int32_t cz,
                               uint8_t cull_decision, uint64_t attempt_id = 0,
                               uint64_t published_rev = 0);
};

} // namespace cutum

#endif
