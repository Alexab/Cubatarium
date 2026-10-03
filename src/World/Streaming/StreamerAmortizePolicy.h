#pragma once
// Stop hang SoT 210431: unload / keep-shell amortize modes under FrameDeadline.

namespace cutum
{

/// UnloadAmortizeMode (RuntimeTuning / streaming_tune.json).
/// 0=U0 baseline full ForEach; 1=U-A Exhausted gates; 2=U-B + scan cursor;
/// 3=U-C + tighter stop gate (caller); 4=U-D + defer save queue.
inline constexpr int kUnloadAmortizeU0 = 0;
inline constexpr int kUnloadAmortizeUA = 1;
inline constexpr int kUnloadAmortizeUB = 2;
inline constexpr int kUnloadAmortizeUC = 3;
inline constexpr int kUnloadAmortizeUD = 4;

/// KeepShellAmortizeMode.
/// 0=K0; 1=K-A Exhausted/frame gate; 2=K-B cheap filter; 3=K-C cursor;
/// 4=K-D disable keep while idle underwater.
inline constexpr int kKeepShellAmortizeK0 = 0;
inline constexpr int kKeepShellAmortizeKA = 1;
inline constexpr int kKeepShellAmortizeKB = 2;
inline constexpr int kKeepShellAmortizeKC = 3;
inline constexpr int kKeepShellAmortizeKD = 4;

inline constexpr int kUnloadScanBudgetPerFrame = 96;
inline constexpr int kKeepShellScanBudgetPerFrame = 64;

/// U-C / caller stop gate: skip unload ForEach after hitch recovery.
inline bool ShouldSkipUnloadOnStopFrame(bool moving, double frame_ms,
                                        bool frame_deadline_exhausted,
                                        int dirty_n, double frame_ms_gate = 12.0,
                                        int dirty_gate = 48)
{
  if (moving)
  {
    return false;
  }
  if (frame_deadline_exhausted)
  {
    return true;
  }
  if (frame_ms > frame_ms_gate)
  {
    return true;
  }
  if (dirty_n > dirty_gate)
  {
    return true;
  }
  return false;
}

/// K-A / K-D caller: skip keep-shell on hot stop or idle underwater.
inline bool ShouldSkipKeepShell(bool frame_deadline_exhausted, double frame_ms,
                                bool idle_underwater, int keep_mode,
                                double frame_ms_gate = 14.0)
{
  if (keep_mode >= kKeepShellAmortizeKA && frame_deadline_exhausted)
  {
    return true;
  }
  if (keep_mode >= kKeepShellAmortizeKA && frame_ms > frame_ms_gate)
  {
    return true;
  }
  if (keep_mode >= kKeepShellAmortizeKD && idle_underwater)
  {
    return true;
  }
  return false;
}

inline bool UnloadModeUsesScanCursor(int mode)
{
  return mode >= kUnloadAmortizeUB;
}

inline bool UnloadModeRespectsExhausted(int mode)
{
  return mode >= kUnloadAmortizeUA;
}

inline bool UnloadModeDefersSave(int mode)
{
  return mode >= kUnloadAmortizeUD;
}

inline bool KeepModeUsesCheapFilter(int mode)
{
  return mode >= kKeepShellAmortizeKB;
}

inline bool KeepModeUsesScanCursor(int mode)
{
  return mode >= kKeepShellAmortizeKC;
}

inline bool KeepModeRespectsExhausted(int mode)
{
  return mode >= kKeepShellAmortizeKA;
}

} // namespace cutum
