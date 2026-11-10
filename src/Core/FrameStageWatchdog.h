#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace cutum
{

/// Default-off heartbeat for locating long-running main-thread stages.
/// Set CUBA_STAGE_WATCHDOG_PATH to an output file to enable it. Optional
/// CUBA_STAGE_WATCHDOG_THRESHOLD_MS and _INTERVAL_MS values make shorter
/// diagnostic hitches observable without changing the default 2s/1s policy.
/// Sub-two-second thresholds emit one line per stage to avoid log floods.
class UFrameStageWatchdog
{
public:
  ~UFrameStageWatchdog()
  {
    Stop_.store(true, std::memory_order_release);
    if (Thread_.joinable())
    {
      Thread_.join();
    }
  }

  class Scope
  {
  public:
    explicit Scope(const char *stage) : Watchdog_(GetIfEnabled())
    {
      if (Watchdog_)
      {
        PreviousStage_ = Watchdog_->CurrentStage_.load(std::memory_order_acquire);
        PreviousStartNs_ =
            Watchdog_->StageStartNs_.load(std::memory_order_acquire);
        Watchdog_->SetStage(stage);
      }
    }

    ~Scope()
    {
      if (Watchdog_)
      {
        Watchdog_->SetStage(PreviousStage_, PreviousStartNs_);
      }
    }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    UFrameStageWatchdog *Watchdog_{nullptr};
    const char *PreviousStage_{nullptr};
    int64_t PreviousStartNs_{0};
  };

  /// Replace the active stage marker without creating a nested RAII scope.
  /// Useful for checkpoints inside a large routine where a stack scope around
  /// each branch would distort or obscure the last non-returning phase.
  static void MarkCurrentStage(const char *stage)
  {
    if (auto *watchdog = GetIfEnabled())
    {
      watchdog->SetStage(stage);
    }
  }

private:
  explicit UFrameStageWatchdog(const char *path)
      : Output_(path, std::ios::out | std::ios::app),
        Origin_(std::chrono::steady_clock::now()),
        ThresholdMs_(ReadPositiveMs("CUBA_STAGE_WATCHDOG_THRESHOLD_MS", 2000)),
        IntervalMs_(ReadPositiveMs("CUBA_STAGE_WATCHDOG_INTERVAL_MS", 1000))
  {
    if (Output_.is_open())
    {
      Output_ << "watchdog_start threshold_ms=" << ThresholdMs_
              << " interval_ms=" << IntervalMs_ << '\n';
      Output_.flush();
      Thread_ = std::thread([this] { Run(); });
    }
  }

  static UFrameStageWatchdog *GetIfEnabled()
  {
    static std::unique_ptr<UFrameStageWatchdog> instance = [] {
      const char *path = std::getenv("CUBA_STAGE_WATCHDOG_PATH");
      if (!path || path[0] == '\0')
      {
        return std::unique_ptr<UFrameStageWatchdog>{};
      }
      auto watchdog = std::unique_ptr<UFrameStageWatchdog>(
          new UFrameStageWatchdog(path));
      if (!watchdog->Output_.is_open())
      {
        return std::unique_ptr<UFrameStageWatchdog>{};
      }
      return watchdog;
    }();
    return instance.get();
  }

  static int ReadPositiveMs(const char *name, const int fallback)
  {
    const char *value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
      return fallback;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 60000)
    {
      return fallback;
    }
    return static_cast<int>(parsed);
  }

  static int64_t NowNs()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  void SetStage(const char *stage)
  {
    SetStage(stage, stage ? NowNs() : 0);
  }

  void SetStage(const char *stage, int64_t start_ns)
  {
    SnapshotSequence_.fetch_add(1, std::memory_order_acq_rel);
    CurrentStage_.store(stage, std::memory_order_relaxed);
    StageStartNs_.store(start_ns, std::memory_order_relaxed);
    SnapshotSequence_.fetch_add(1, std::memory_order_release);
  }

  void Run()
  {
    int64_t last_reported_stage_start_ns = 0;
    while (!Stop_.load(std::memory_order_acquire))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(IntervalMs_));
      const uint64_t sequence_before =
          SnapshotSequence_.load(std::memory_order_acquire);
      if ((sequence_before & 1U) != 0)
      {
        continue;
      }
      const char *stage = CurrentStage_.load(std::memory_order_acquire);
      const int64_t start_ns = StageStartNs_.load(std::memory_order_acquire);
      const uint64_t sequence_after =
          SnapshotSequence_.load(std::memory_order_acquire);
      if (sequence_before != sequence_after)
      {
        continue;
      }
      if (!stage || start_ns == 0)
      {
        continue;
      }
      const int64_t now_ns = NowNs();
      const int64_t stage_ms = (now_ns - start_ns) / 1000000;
      const bool report_once_per_stage = ThresholdMs_ < 2000;
      if (stage_ms < ThresholdMs_ ||
          (report_once_per_stage &&
           start_ns == last_reported_stage_start_ns))
      {
        continue;
      }
      const int64_t elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - Origin_)
              .count();
      Output_ << "stage_heartbeat elapsed_ms=" << elapsed_ms
              << " stage=" << stage << " stage_ms=" << stage_ms << '\n';
      Output_.flush();
      if (report_once_per_stage)
      {
        last_reported_stage_start_ns = start_ns;
      }
    }
  }

  std::ofstream Output_;
  const std::chrono::steady_clock::time_point Origin_;
  const int ThresholdMs_;
  const int IntervalMs_;
  std::atomic<const char *> CurrentStage_{nullptr};
  std::atomic<int64_t> StageStartNs_{0};
  std::atomic<uint64_t> SnapshotSequence_{0};
  std::atomic<bool> Stop_{false};
  std::thread Thread_;
};

} // namespace cutum
