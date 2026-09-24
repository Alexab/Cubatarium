#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace cutum
{

/// A41: exclusive visual SoT class (Hide ⇒ one class with attempt+SLA).
enum class VisualObligation : uint8_t
{
  None = 0,
  LitDrawable,
  LegalDark,
  LightRepair,
  GeomRepair,
  SoftDeferOwned
};

struct VisualObligationShadowCounters
{
  uint64_t samples{0};
  uint64_t draw_mismatches{0};
};

inline VisualObligationShadowCounters &GetVisualObligationShadowCounters()
{
  static VisualObligationShadowCounters counters;
  return counters;
}

/// Shadow is opt-in because the per-slice classifier adds work to the draw gate.
inline bool VisualObligationShadowEnabled()
{
  static const bool enabled = []() {
    if (const char *env = std::getenv("CUBA_VISUAL_OBLIGATION_SHADOW"))
    {
      return env[0] == '1' || env[0] == 't' || env[0] == 'T';
    }
    return false;
  }();
  return enabled;
}

inline bool VisualObligationCutoverEnabled()
{
  static const bool enabled = []() {
    if (const char *env = std::getenv("CUBA_VISUAL_OBLIGATION_CUTOVER"))
    {
      return env[0] == '1' || env[0] == 't' || env[0] == 'T';
    }
    return false;
  }();
  return enabled;
}

/// Classify obligation from observational predicates. Callers stamp ColumnRecord.
/// Priority: lit > LegalDark > LightRepair > GeomRepair > SoftDeferOwned.
inline VisualObligation ClassifyVisualObligation(bool has_lit_drawable,
                                                 bool fully_dark,
                                                 bool still_stale,
                                                 bool open_sky,
                                                 bool legal_dark_settled,
                                                 bool soft_defer_held,
                                                 bool geom_unsat)
{
  if (has_lit_drawable)
  {
    return VisualObligation::LitDrawable;
  }
  if (legal_dark_settled ||
      (fully_dark && !still_stale && !open_sky))
  {
    return VisualObligation::LegalDark;
  }
  if (still_stale || (fully_dark && !still_stale && open_sky))
  {
    return VisualObligation::LightRepair;
  }
  if (geom_unsat)
  {
    return VisualObligation::GeomRepair;
  }
  if (soft_defer_held)
  {
    return VisualObligation::SoftDeferOwned;
  }
  return VisualObligation::None;
}

inline bool VisualObligationAllowsDraw(VisualObligation o)
{
  return o == VisualObligation::LitDrawable ||
         o == VisualObligation::LegalDark;
}

inline bool VisualObligationIsRepair(VisualObligation o)
{
  return o == VisualObligation::LightRepair ||
         o == VisualObligation::GeomRepair ||
         o == VisualObligation::SoftDeferOwned;
}

/// SoftDefer may hold/hide only when a repair ticket already exists.
inline bool SoftDeferHoldAllowedWithTicket(bool soft_defer_would_hold,
                                           bool has_repair_ticket)
{
  return !soft_defer_would_hold || has_repair_ticket;
}

/// A42: SoftDefer schedule must not RemoveAt drawable remesh under LightRepair —
/// that Dirty is the Published owner while PL is kept. SoftDefer still blocks
/// FirstMesh/unlit publish; this predicate only allows schedule fallthrough.
/// A42c: FullyDark drawable remesh is the same Published owner when stamp lags
/// (not LegalDark — intentional dark stays SoftDefer-blocked).
/// A42d: StaleVertexLight drawable remesh likewise (VB≈StaleVL after FD drain).
inline bool SoftDeferAllowsLightRepairRemesh(bool soft_defer_active,
                                             bool has_drawable,
                                             VisualObligation obligation,
                                             bool fully_dark = false,
                                             bool stale_vertex_light = false)
{
  if (!soft_defer_active)
  {
    return true;
  }
  if (!has_drawable)
  {
    return false;
  }
  if (obligation == VisualObligation::LightRepair)
  {
    return true;
  }
  if (obligation == VisualObligation::LegalDark)
  {
    return false;
  }
  return fully_dark || stale_vertex_light;
}

/// A41 LightRepair: open_sky equal-rev FullyDark needs one Dirty remesh.
inline bool NeedsOpenSkyEqualRevLightRepair(bool fully_dark, bool still_stale,
                                            bool open_sky)
{
  return fully_dark && !still_stale && open_sky;
}

inline bool IsLegalDarkEqualRevFullyDark(bool fully_dark, bool still_stale,
                                         bool open_sky)
{
  return fully_dark && !still_stale && !open_sky;
}

/// Monotonic ms for VisualObligation SLA (process-local).
inline double VisualObligationNowMs()
{
  using clock = std::chrono::steady_clock;
  static const clock::time_point t0 = clock::now();
  return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
}

constexpr double kLightRepairSlaMs = 3000.0;

inline double StampLightRepairDeadlineMs(double now_ms)
{
  return now_ms + kLightRepairSlaMs;
}

/// Live pipeline owner excludes Dirty-only (skip_snapshot can park Dirty forever).
inline bool LightRepairHasLivePipeline(bool raa_pending, bool gpu_pending,
                                       bool inflight)
{
  return raa_pending || gpu_pending || inflight;
}

/// Remint LightRepair Dirty: first mint, expired SLA, or legacy deadline<=0.
/// Never remint while Capture/GPU/inflight is live (coalesce). Dirty-only does
/// not block — manual 132520 plateau was Dirty stuck in skip_snapshot.
inline bool ShouldRemintLightRepairDirty(bool light_repair_obligation,
                                         bool live_pipeline,
                                         uint64_t attempt_id,
                                         double deadline_ms, double now_ms)
{
  if (!light_repair_obligation || live_pipeline)
  {
    return false;
  }
  if (attempt_id == 0)
  {
    return true;
  }
  if (deadline_ms <= 0.0)
  {
    return true;
  }
  return now_ms >= deadline_ms;
}

} // namespace cutum
