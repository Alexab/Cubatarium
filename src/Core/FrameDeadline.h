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

  /// Soft defer for Relight/Seam/non-critical drains. Pass
  /// `critical_progress=true` for FirstMesh so Exhausted never hard-kills FM.
  static bool ShouldDeferProducer(bool critical_progress)
  {
    return !critical_progress && Get().Exhausted();
  }

private:
  double BudgetMs_{0.0};
  std::chrono::steady_clock::time_point Start_{};
};

} // namespace cutum

#endif // FRAMEDEADLINE_H
