#ifndef FRAMEDEADLINE_H
#define FRAMEDEADLINE_H

#include <chrono>
#include <cstdint>
#include <cstddef>

namespace cutum
{

/// Q8: single frame work budget shared by capture/apply/admit producers.
/// Soft contract — callers check RemainingMs() and defer; not a hard preemption.
class UFrameDeadline
{
public:
  static UFrameDeadline &Get()
  {
    static UFrameDeadline instance;
    return instance;
  }

  void BeginFrame(double budget_ms)
  {
    BudgetMs_ = budget_ms > 0.0 ? budget_ms : 0.0;
    Start_ = std::chrono::steady_clock::now();
    CriticalUnitsThisFrame_ = 0;
    CriticalUnitExceededN_ = 0;
    LastCriticalUnitMs_ = 0.0;
  }

  double BudgetMs() const { return BudgetMs_; }

  /// A21 P5: optional soft cap for one critical unit after Exhausted.
  /// 0 = undocumented / unlimited (legacy). Production may set ~4–8 ms.
  void SetMaxCriticalUnitMs(double ms)
  {
    MaxCriticalUnitMs_ = ms > 0.0 ? ms : 0.0;
  }
  double MaxCriticalUnitMs() const { return MaxCriticalUnitMs_; }

  double ElapsedMs() const
  {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - Start_).count();
  }

  double RemainingMs() const
  {
    if (BudgetMs_ <= 0.0)
    {
      return 1.0e9;
    }
    const double left = BudgetMs_ - ElapsedMs();
    return left > 0.0 ? left : 0.0;
  }

  bool Exhausted() const { return RemainingMs() <= 0.0; }

  /// Ownership HitchBudget: defer secondary scans (SoftDefer disk, thrash)
  /// when leftover under min_remaining_ms. Critical FirstMesh uses
  /// ShouldDeferProducer instead.
  static bool ShouldDeferSecondaryScan(double min_remaining_ms = 2.0)
  {
    auto &dl = Get();
    if (dl.Exhausted())
    {
      return true;
    }
    return dl.RemainingMs() < min_remaining_ms;
  }

  /// Soft defer for Relight/Seam/non-critical drains. Pass
  /// `critical_progress=true` for FirstMesh — allows a bounded overrun of one
  /// critical unit after Exhausted (audit R08), not infinite bypass.
  /// When MaxCriticalUnitMs > 0, callers should NoteCriticalUnitFinished so
  /// overruns are counted (cost gate for measured-bounded quantum).
  static bool ShouldDeferProducer(bool critical_progress)
  {
    auto &dl = Get();
    if (!dl.Exhausted())
    {
      return false;
    }
    if (!critical_progress)
    {
      return true;
    }
    // One non-preemptible critical unit after budget exhaust.
    if (dl.CriticalUnitsThisFrame_ >= 1)
    {
      return true;
    }
    ++dl.CriticalUnitsThisFrame_;
    dl.CriticalUnitStart_ = std::chrono::steady_clock::now();
    return false;
  }

  /// Call after finishing a critical unit started via ShouldDeferProducer(true).
  /// Records elapsed; if MaxCriticalUnitMs set and exceeded, notes overrun.
  static void NoteCriticalUnitFinished()
  {
    auto &dl = Get();
    if (dl.CriticalUnitsThisFrame_ <= 0)
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    dl.LastCriticalUnitMs_ =
        std::chrono::duration<double, std::milli>(now - dl.CriticalUnitStart_)
            .count();
    if (dl.MaxCriticalUnitMs_ > 0.0 &&
        dl.LastCriticalUnitMs_ > dl.MaxCriticalUnitMs_)
    {
      ++dl.CriticalUnitExceededN_;
    }
  }

  double LastCriticalUnitMs() const { return LastCriticalUnitMs_; }
  int CriticalUnitExceededN() const { return CriticalUnitExceededN_; }

private:
  double BudgetMs_{0.0};
  double MaxCriticalUnitMs_{4.0}; // A22 S6: default 4ms critical unit cap
  std::chrono::steady_clock::time_point Start_{};
  std::chrono::steady_clock::time_point CriticalUnitStart_{};
  int CriticalUnitsThisFrame_{0};
  int CriticalUnitExceededN_{0};
  double LastCriticalUnitMs_{0.0};
};

/// A26 N4: resumable main-thread unit cursor — preserves progress across frames.
struct ResumableWorkCursor
{
  uint64_t manifest_generation{0};
  size_t index{0};
  size_t total{0};
  bool active{false};
};

inline bool ShouldResumeWorkCursor(const ResumableWorkCursor &c,
                                   uint64_t live_manifest_generation)
{
  return c.active && c.manifest_generation == live_manifest_generation &&
         c.index < c.total;
}

inline void AdvanceWorkCursor(ResumableWorkCursor &c, size_t n)
{
  if (!c.active)
  {
    return;
  }
  if (c.index + n >= c.total)
  {
    c.index = c.total;
    c.active = false;
    return;
  }
  c.index += n;
}

/// A26 N4: unified admission pool budgets (snapshots / GPU / retirement).
struct UnifiedAdmissionPools
{
  int snapshot_slots{8};
  int queued_output_slots{16};
  int gpu_inflight_slots{8};
  int retirement_slots{8};
};

inline bool CanAdmitUnifiedWork(const UnifiedAdmissionPools &pools,
                                int snapshot_used, int queued_used,
                                int gpu_used, int retirement_used)
{
  return snapshot_used < pools.snapshot_slots &&
         queued_used < pools.queued_output_slots &&
         gpu_used < pools.gpu_inflight_slots &&
         retirement_used < pools.retirement_slots;
}

} // namespace cutum

#endif // FRAMEDEADLINE_H
