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
  Count
};

struct JobStageSpan
{
  int32_t cx{0};
  int32_t cy{0};
  int32_t cz{0};
  uint32_t incarnation{0};
  uint64_t attempt_id{0};
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
};

class UJobStageTrace
{
public:
  static constexpr size_t kRingCapacity = 256;

  static void Note(const JobStageSpan &span);
  static size_t Size();
  static bool Get(size_t newest_index, JobStageSpan &out);
  /// Emit compact JSONL lines (kind=job_trace) into an open ostream-like sink
  /// via callback; used by FramePerfMonitor emergency dump.
  static void ForEachNewest(size_t max_n,
                            void (*fn)(const JobStageSpan &, void *), void *ctx);
  static const char *StageName(JobStage s);
  /// A36 S1: note cull exclusion for a tracked chunk (bounded ring).
  static void NoteCullDecision(int32_t cx, int32_t cy, int32_t cz,
                               uint8_t cull_decision, uint64_t attempt_id = 0,
                               uint64_t published_rev = 0);
};

} // namespace cutum

#endif
