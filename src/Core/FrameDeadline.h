#ifndef FRAMEDEADLINE_H
#define FRAMEDEADLINE_H

#include <chrono>
#include <cstdint>

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
  }

  double BudgetMs() const { return BudgetMs_; }

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
    return false;
  }

private:
  double BudgetMs_{0.0};
  std::chrono::steady_clock::time_point Start_{};
  int CriticalUnitsThisFrame_{0};
};

} // namespace cutum

#endif // FRAMEDEADLINE_H
