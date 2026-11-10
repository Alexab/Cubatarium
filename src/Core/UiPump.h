#ifndef UIPUMP_H
#define UIPUMP_H

#include <chrono>
#include <functional>

namespace cutum
{

/// Keeps the window alive during unavoidable long blocking work on the main
/// thread (startup, resource pack application). Whoever owns the window
/// installs a handler once; long loops call Pump() as they make progress. The
/// handler only runs while installed and at most every `MinIntervalMs`, so
/// calling Pump() from a hot loop is cheap. With no handler it is a no-op.
class UBlockingWorkPump
{
public:
  using Handler = std::function<void(const char *label, float fraction)>;

  static void Install(Handler handler, double min_interval_ms = 33.0)
  {
    State &s = Get();
    s.Fn = std::move(handler);
    s.MinIntervalMs = min_interval_ms;
    s.Last = std::chrono::steady_clock::time_point{};
  }

  static void Clear() { Get().Fn = nullptr; }

  /// `fraction` < 0 means "unknown".
  static void Pump(const char *label, float fraction = -1.0f)
  {
    State &s = Get();
    if (!s.Fn || s.InHandler)
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double, std::milli>(now - s.Last).count() <
        s.MinIntervalMs)
    {
      return;
    }
    s.Last = now;
    s.InHandler = true;
    s.Fn(label, fraction);
    s.InHandler = false;
  }

private:
  struct State
  {
    Handler Fn;
    double MinIntervalMs{33.0};
    std::chrono::steady_clock::time_point Last{};
    bool InHandler{false};
  };
  static State &Get()
  {
    static State s;
    return s;
  }
};

} // namespace cutum

#endif
