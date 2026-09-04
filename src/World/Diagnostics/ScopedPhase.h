#pragma once

#include <chrono>

namespace cutum
{

/// RAII chrono bracket that writes elapsed ms into *OutMs on destruction.
/// Use instead of manual lap_ms(t0) so parent self-time is explicit
/// (parent_total - sum(children)), not a subtracted "gap" leftover.
class ScopedPhase
{
public:
  explicit ScopedPhase(double *out_ms)
      : OutMs(out_ms), T0(std::chrono::high_resolution_clock::now())
  {
    if (OutMs)
    {
      *OutMs = 0.0;
    }
  }

  ScopedPhase(const ScopedPhase &) = delete;
  ScopedPhase &operator=(const ScopedPhase &) = delete;

  ~ScopedPhase()
  {
    if (!OutMs)
    {
      return;
    }
    *OutMs = std::chrono::duration<double, std::milli>(
                 std::chrono::high_resolution_clock::now() - T0)
                 .count();
  }

private:
  double *OutMs{nullptr};
  std::chrono::high_resolution_clock::time_point T0;
};

/// Parent phase: latches total on destroy, computes SelfMs = Total - ChildSum.
class ScopedParentPhase
{
public:
  ScopedParentPhase(double *total_ms, double *self_ms)
      : TotalMs(total_ms), SelfMs(self_ms),
        T0(std::chrono::high_resolution_clock::now())
  {
    if (TotalMs)
    {
      *TotalMs = 0.0;
    }
    if (SelfMs)
    {
      *SelfMs = 0.0;
    }
  }

  ScopedParentPhase(const ScopedParentPhase &) = delete;
  ScopedParentPhase &operator=(const ScopedParentPhase &) = delete;

  void AddChild(double child_ms) { ChildSum += child_ms; }

  /// Call after children finish (or rely on destructor if children wrote
  /// into fields you sum explicitly via Finalize).
  void Finalize(double accounted_children_ms)
  {
    const double total = std::chrono::duration<double, std::milli>(
                             std::chrono::high_resolution_clock::now() - T0)
                             .count();
    if (TotalMs)
    {
      *TotalMs = total;
    }
    if (SelfMs)
    {
      const double self = total - accounted_children_ms;
      *SelfMs = self > 0.0 ? self : 0.0;
    }
    Finalized = true;
  }

  ~ScopedParentPhase()
  {
    if (!Finalized)
    {
      Finalize(ChildSum);
    }
  }

private:
  double *TotalMs{nullptr};
  double *SelfMs{nullptr};
  double ChildSum{0.0};
  bool Finalized{false};
  std::chrono::high_resolution_clock::time_point T0;
};

} // namespace cutum
