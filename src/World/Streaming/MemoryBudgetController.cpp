#include "World/Streaming/MemoryBudgetController.h"

#include <algorithm>

namespace cutum
{

MemoryBudgetDecision
UMemoryBudgetController::Evaluate(const MemoryBudgetSample &sample,
                                  const URuntimeTuning &tuning)
{
  MemoryBudgetDecision d;
  d.keep_margin = sample.baseline_keep_margin;
  d.max_effective_rd = sample.visual_rd;
  d.allow_keep_prewarm = true;
  d.emergency_cancel_outside = false;
  d.capture_hard_cap = -1;

  const double budget = static_cast<double>(tuning.MemoryBudgetMb);
  const double soft = static_cast<double>(tuning.MemorySoftMb);
  const double expand = static_cast<double>(tuning.MemoryExpandKeepMb);

  if (sample.private_mb >= budget)
  {
    d.memory_pressure = 2;
  }
  else if (sample.private_mb >= soft)
  {
    d.memory_pressure = 1;
  }
  else
  {
    d.memory_pressure = 0;
  }
  // Soft/Hard is byte-budget only. Do NOT OR stream Yellow/Red into
  // memory_pressure — that blocked Keep expand at ~0.5 GB private while
  // StreamingPressure stayed Yellow (manual 20260723-081832).

  const bool green_expand =
      d.memory_pressure == 0 && sample.visual_holes == 0 &&
      sample.pending_light_focus == 0 && sample.last_wall_ms <= 28.0 &&
      sample.private_mb < expand;

  if (green_expand)
  {
    d.keep_margin =
        std::min(tuning.MaxKeepPrefetchMargin,
                 std::max(sample.baseline_keep_margin, sample.baseline_keep_margin + 1));
    d.max_effective_rd =
        std::min(tuning.MemoryExpandMaxRd,
                 std::max(sample.visual_rd, sample.visual_rd + 1));
    d.allow_keep_prewarm = true;
  }
  else if (d.memory_pressure >= 2)
  {
    d.keep_margin = sample.baseline_keep_margin;
    d.max_effective_rd = sample.visual_rd;
    d.allow_keep_prewarm = false;
    d.emergency_cancel_outside = true;
    d.capture_hard_cap = 1;
  }
  else if (d.memory_pressure >= 1)
  {
    d.keep_margin = sample.baseline_keep_margin;
    d.max_effective_rd = sample.visual_rd;
    d.allow_keep_prewarm = false;
    d.capture_hard_cap = 2;
  }

  // Hitch gate (manual 085228): seconds-scale Capture while holes=1 and
  // memory_pressure stayed 0 — tighten Capture even under byte-budget Green.
  if (d.capture_hard_cap < 0 && sample.visual_holes > 0)
  {
    d.capture_hard_cap = 1;
  }
  else if (d.capture_hard_cap < 0 && sample.last_wall_ms > 500.0)
  {
    d.capture_hard_cap = 1;
  }
  // V5 ring SLA: focus relight debt — cap capture when pending trail high.
  if (d.capture_hard_cap < 0 && sample.pending_light_focus > 15)
  {
    d.capture_hard_cap = 2;
  }

  return d;
}

bool UMemoryBudgetController::MaybeEvaluate(int frame_counter,
                                            const MemoryBudgetSample &sample,
                                            const URuntimeTuning &tuning,
                                            MemoryBudgetDecision &out)
{
  // Holes / hitch: re-evaluate immediately so capture_hard_cap applies this
  // frame (manual 091724: 20-frame lag left Capture uncapped into a hole).
  const bool urgent =
      sample.visual_holes > 0 || sample.last_wall_ms > 100.0;
  if (!urgent && frame_counter - LastEvalFrame < kEvalIntervalFrames)
  {
    out = LastDecision;
    return false;
  }
  LastEvalFrame = frame_counter;
  LastDecision = Evaluate(sample, tuning);
  out = LastDecision;
  return true;
}

} // namespace cutum
