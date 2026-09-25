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
  /// 0=black-attribution sample, 1=focus solid-slice visibility sample.
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
  uint64_t incarnation{0};
  uint64_t chunk_content_revision{0};
  uint64_t mesh_revision{0};
  uint64_t attempt_id{0};
  uint64_t desired_geom_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t demand_published_geom_rev{0};
  uint64_t demand_published_light_rev{0};
  uint64_t published_geom_rev{0};
  uint64_t published_light_rev{0};
  uint64_t meshed_light_rev{0};
  uint64_t field_light_rev{0};
  /// VisibleBlackCause ordinal from VisibleBlackAttribution.h.
  uint8_t cause{0};
  uint8_t active_stage{0};
  uint8_t face_debt_mask{0};
  uint8_t draw_gate_ready{0};
  /// sample_kind=0 bits: ticket, progress, sticky, pending_replace,
  /// column_light_revs_match, drawable, any_dark_face, dirty,
  /// remesh_after_apply, gpu_pending, inflight, column_has_stale_dark,
  /// gpu_resident, slice_stale_dark, lit_drawable, active_attempt.
  /// sample_kind=1 bits: drawable, satisfying, dirty, inflight, gpu_pending,
  /// gpu_extract, live_gpu, draw_gate_ready, remesh_after_apply.
  uint16_t flags{0};
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
