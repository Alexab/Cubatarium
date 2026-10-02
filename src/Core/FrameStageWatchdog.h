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
/// Set CUBA_STAGE_WATCHDOG_PATH to an output file to enable it. The monitor
/// thread writes a flushed line every second while a stage lasts at least two
/// seconds, so a stuck frame cannot hide the last active stage in perf JSONL.
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

private:
  explicit UFrameStageWatchdog(const char *path)
      : Output_(path, std::ios::out | std::ios::app),
        Origin_(std::chrono::steady_clock::now())
  {
    if (Output_.is_open())
    {
      Output_ << "watchdog_start threshold_ms=2000 interval_ms=1000\n";
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
    while (!Stop_.load(std::memory_order_acquire))
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
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
      if (stage_ms < 2000)
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
    }
  }

  std::ofstream Output_;
  const std::chrono::steady_clock::time_point Origin_;
  std::atomic<const char *> CurrentStage_{nullptr};
  std::atomic<int64_t> StageStartNs_{0};
  std::atomic<uint64_t> SnapshotSequence_{0};
  std::atomic<bool> Stop_{false};
  std::thread Thread_;
};

} // namespace cutum
