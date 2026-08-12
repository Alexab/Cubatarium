#include "World/Diagnostics/EnterLitDiagnostics.h"

#include "App/Platform/Log.h"
#include "World/Core/World.h"
#include "World/Persistence/WorldPersistence.h"
#include "glog/logging.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace cutum
{

namespace
{

std::mutex g_session_mutex;
std::ofstream g_jsonl;
bool g_session_open{false};
std::chrono::steady_clock::time_point g_session_start{};
double g_last_heartbeat_elapsed_ms{-1.0};

std::string MakeSessionPath()
{
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  std::ostringstream oss;
  oss << "logs/enter_lit_"
      << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") << ".jsonl";
  return oss.str();
}

void WriteJsonlLine(const EnterLitSample &s)
{
  if (!g_jsonl.is_open())
  {
    return;
  }
  g_jsonl << "{\"elapsed_ms\":" << s.elapsed_ms << ",\"snapshot_debt\":"
          << s.snapshot_debt << ",\"snapshot_size\":" << s.snapshot_size
          << ",\"pending_global\":" << s.pending_global << ",\"fifo_n\":"
          << s.fifo_n << ",\"inflight\":" << s.inflight
          << ",\"chunk_resident\":" << s.chunk_resident
          << ",\"streaming_frozen\":" << (s.streaming_frozen ? 1 : 0)
          << ",\"mesh_dirty\":" << (s.mesh_dirty ? 1 : 0)
          << ",\"mesh_missing_greedy\":" << (s.mesh_missing_greedy ? 1 : 0)
          << ",\"mesh_gpu_pending_near\":" << s.mesh_gpu_pending_near
          << ",\"mesh_async_pending\":" << (s.mesh_async_pending ? 1 : 0)
          << ",\"mesh_visual_warmup\":" << (s.mesh_visual_warmup ? 1 : 0)
          << "}\n";
  g_jsonl.flush();
}

void LogSampleGlog(const EnterLitSample &sample, int frame_index)
{
  LOG(INFO) << "[EnterLit] frame=" << frame_index
            << " elapsed_ms=" << sample.elapsed_ms
            << " debt=" << sample.snapshot_debt
            << " snap=" << sample.snapshot_size
            << " pending_global=" << sample.pending_global
            << " fifo=" << sample.fifo_n << " inflight=" << sample.inflight
            << " chunks=" << sample.chunk_resident
            << " frozen=" << (sample.streaming_frozen ? 1 : 0)
            << " mesh_dirty=" << (sample.mesh_dirty ? 1 : 0)
            << " mesh_missing=" << (sample.mesh_missing_greedy ? 1 : 0)
            << " gpu_pending=" << sample.mesh_gpu_pending_near
            << " mesh_async=" << (sample.mesh_async_pending ? 1 : 0)
            << " visual_warmup=" << (sample.mesh_visual_warmup ? 1 : 0);
  CubatariumFlushLogs();
}

} // namespace

void UEnterLitDiagnostics::BeginSession()
{
  std::lock_guard<std::mutex> lock(g_session_mutex);
  EndSession();
  g_session_start = std::chrono::steady_clock::now();
  g_last_heartbeat_elapsed_ms = -1.0;
  std::error_code ec;
  std::filesystem::create_directories("logs", ec);
  g_jsonl.open(MakeSessionPath(), std::ios::out | std::ios::trunc);
  g_session_open = g_jsonl.is_open();
}

void UEnterLitDiagnostics::EndSession()
{
  if (g_jsonl.is_open())
  {
    g_jsonl.close();
  }
  g_session_open = false;
  g_last_heartbeat_elapsed_ms = -1.0;
}

void UEnterLitDiagnostics::Sample(UWorld &world, double elapsed_ms,
                                  EnterLitSample &out)
{
  out.elapsed_ms = elapsed_ms;
  out.snapshot_debt = world.CountEnterFovLitDebt();
  out.snapshot_size = world.GetEnterLitSnapshotSize();
  out.pending_global = static_cast<int>(world.GetPendingLightBeforeMeshCount());
  out.fifo_n = world.GetPendingTerrainRelightFifoCount();
  out.inflight = world.GetAsyncRelightInFlightCount();
  out.chunk_resident =
      static_cast<int>(world.GetBlockWorld().GetChunkManager().GetResidentChunkCount());
  out.streaming_frozen = world.IsEnterLitGateActive();
  UWorld::EnterGameMeshWarmupBlockers blockers{};
  world.SampleEnterGameMeshWarmupBlockers(blockers);
  out.mesh_dirty = blockers.dirty;
  out.mesh_missing_greedy = blockers.missing_greedy;
  out.mesh_gpu_pending_near = blockers.gpu_pending_near;
  out.mesh_async_pending = blockers.async_mesh_pending;
  out.mesh_visual_warmup = blockers.visual_warmup;
}

void UEnterLitDiagnostics::MaybeLog(const EnterLitSample &sample,
                                    int frame_index, int every_n_frames)
{
  if (every_n_frames <= 0)
  {
    return;
  }
  const bool early_frames = frame_index <= 5;
  const bool periodic =
      frame_index == 0 || (frame_index > 0 && frame_index % every_n_frames == 0);
  if (!early_frames && !periodic)
  {
    return;
  }
  LogSampleGlog(sample, frame_index);
  std::lock_guard<std::mutex> lock(g_session_mutex);
  if (g_session_open)
  {
    WriteJsonlLine(sample);
  }
}

void UEnterLitDiagnostics::MaybeLogHeartbeat(const EnterLitSample &sample,
                                             double heartbeat_ms)
{
  if (heartbeat_ms <= 0.0)
  {
    return;
  }
  if (g_last_heartbeat_elapsed_ms >= 0.0 &&
      sample.elapsed_ms - g_last_heartbeat_elapsed_ms < heartbeat_ms)
  {
    return;
  }
  g_last_heartbeat_elapsed_ms = sample.elapsed_ms;
  LOG(INFO) << "[EnterWarmup] heartbeat elapsed_ms=" << sample.elapsed_ms
            << " debt=" << sample.snapshot_debt
            << " mesh_dirty=" << (sample.mesh_dirty ? 1 : 0)
            << " mesh_missing=" << (sample.mesh_missing_greedy ? 1 : 0)
            << " gpu_pending=" << sample.mesh_gpu_pending_near
            << " mesh_async=" << (sample.mesh_async_pending ? 1 : 0)
            << " visual_warmup=" << (sample.mesh_visual_warmup ? 1 : 0);
  CubatariumFlushLogs();
  std::lock_guard<std::mutex> lock(g_session_mutex);
  if (g_session_open)
  {
    WriteJsonlLine(sample);
  }
}

} // namespace cutum
