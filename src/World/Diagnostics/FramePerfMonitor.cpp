#include "World/Diagnostics/FramePerfMonitor.h"

#include "App/Core.h"
#include "World/Core/World.h"
#include "World/Physics/PhysicsTelemetry.h"
#include "glog/logging.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cutum
{
namespace
{

struct Session
{
  std::mutex Mutex;
  std::ofstream Jsonl;
  std::string Path;
  bool Opened{false};
  double AccumWallMs{0.0};
  double AccumSimMs{0.0};
  double AccumSwapMs{0.0};
  double AccumUnaccountedMs{0.0};
  double AccumInputMs{0.0};
  double AccumAppUpdateMs{0.0};
  double AccumWorldExtraMs{0.0};
  double AccumPrepareMs{0.0};
  double AccumPostSceneMs{0.0};
  double AccumGuiMs{0.0};
  double AccumResidualMs{0.0};
  double AccumFluidCpuMs{0.0};
  double AccumFluidGpuMs{0.0};
  double MaxWallMs{0.0};
  int FrameCount{0};
  std::chrono::steady_clock::time_point LastEmit{
      std::chrono::steady_clock::now()};
};

Session &GetSession()
{
  static Session s;
  return s;
}

void OpenSessionLocked(Session &s)
{
  if (s.Opened)
  {
    return;
  }
  const auto logs = GetExecutableDirectory() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(logs, ec);
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream name;
#ifdef _WIN32
  const long long pid = static_cast<long long>(GetCurrentProcessId());
#else
  const long long pid = static_cast<long long>(getpid());
#endif
  name << "perf_" << std::put_time(&tm, "%Y%m%d-%H%M%S") << "_" << pid
       << ".jsonl";
  s.Path = (logs / name.str()).string();
  s.Jsonl.open(s.Path, std::ios::out | std::ios::app);
  s.Opened = s.Jsonl.is_open();
  if (s.Opened)
  {
    LOG(INFO) << "[Perf] session file=" << s.Path;
  }
  else
  {
    LOG(WARNING) << "[Perf] failed to open " << s.Path;
  }
}

struct FrameNumbers
{
  double wall_ms{0.0};
  double sim_ms{0.0};
  double swap_wait_ms{0.0};
  double unaccounted_ms{0.0};
  double phys_ms{0.0};
  double stream_ms{0.0};
  double mesh_emerge_ms{0.0};
  double scene_ms{0.0};
  double view_ms{0.0};
  double flat_ms{0.0};
  double input_ms{0.0};
  double app_update_ms{0.0};
  double world_extra_ms{0.0};
  double prepare_frame_ms{0.0};
  double post_scene_ms{0.0};
  double gui_overlay_ms{0.0};
  double residual_ms{0.0};
  double fluid_map_cpu_ms{0.0};
  double fluid_map_gpu_ms{0.0};
  int fluid_map_dirty{0};
  int fluid_map_full_rebuild{0};
  double commit_apply_ms{0.0};
  double commit_seal_ms{0.0};
  double streamer_update_ms{0.0};
  double async_io_ms{0.0};
  double relight_drain_ms{0.0};
  double mesh_sync_ms{0.0};
  double mesh_snapshot_ms{0.0};
  int keep_cols{0};
  int visual_cols{0};
  double idle_prefetch_ms{0.0};
  int prefetch_visual_ops{0};
  int prefetch_keep_ops{0};
  int gen_backlog_total{0};
  int gen_q{0};
  int mesh_async{0};
  int dirty{0};
};

FrameNumbers Compute(UWorld &world, double swap_wait_ms)
{
  FrameNumbers n;
  const PhysicsTelemetry &phys = world.GetPhysicsTelemetry();
  n.wall_ms = world.GetWallFrameDelta() * 1000.0;
  n.phys_ms = phys.PhysicsStepMs;
  n.stream_ms = phys.StreamMs;
  n.mesh_emerge_ms = phys.MeshEmergeMs;
  n.scene_ms = world.GetDurationDrawSceneMks() / 1000.0;
  n.view_ms = world.GetDurationViewUpdateMks() / 1000.0;
  n.sim_ms = n.phys_ms + n.stream_ms + n.mesh_emerge_ms + n.view_ms + n.scene_ms;
  n.swap_wait_ms = swap_wait_ms;
  n.unaccounted_ms = n.wall_ms - n.sim_ms - n.swap_wait_ms;
  if (n.unaccounted_ms < 0.0 && n.unaccounted_ms > -1.0)
  {
    n.unaccounted_ms = 0.0;
  }
  n.input_ms = world.GetLastInputMs();
  n.app_update_ms = world.GetLastAppUpdateMs();
  // World tick includes DoMovement (phys); only the overhead is "unaccounted".
  n.world_extra_ms =
      (std::max)(0.0,
                 world.GetLastWorldTickMs() - n.phys_ms - n.stream_ms -
                     n.mesh_emerge_ms);
  n.prepare_frame_ms = world.GetLastPrepareFrameMs();
  n.post_scene_ms = world.GetLastPostSceneMs();
  n.gui_overlay_ms = world.GetLastGuiOverlayMs();
  n.fluid_map_cpu_ms = world.GetLastFluidMapCpuMs();
  n.fluid_map_gpu_ms = world.GetLastFluidMapGpuMs();
  n.fluid_map_dirty = world.GetLastFluidMapDirtyChunks();
  n.fluid_map_full_rebuild = world.GetLastFluidMapFullRebuild() ? 1 : 0;
  n.commit_apply_ms = phys.CommitApplyMs;
  n.commit_seal_ms = phys.CommitSealMs;
  n.streamer_update_ms = phys.StreamerUpdateMs;
  n.async_io_ms = phys.AsyncIoMs;
  n.relight_drain_ms = phys.RelightDrainMs;
  n.mesh_sync_ms = phys.MeshSyncMs;
  n.mesh_snapshot_ms = phys.MeshSnapshotMs;
  n.keep_cols = phys.KeepCols;
  n.visual_cols = phys.VisualCols;
  n.idle_prefetch_ms = phys.IdlePrefetchMs;
  n.prefetch_visual_ops = phys.PrefetchVisualOps;
  n.prefetch_keep_ops = phys.PrefetchKeepOps;
  n.gen_backlog_total = phys.GenBacklogTotal;
  n.residual_ms = n.unaccounted_ms - n.input_ms - n.app_update_ms -
                  n.world_extra_ms - n.prepare_frame_ms - n.post_scene_ms -
                  n.gui_overlay_ms;
  const auto &md = world.GetMovementDiagnostics();
  n.flat_ms = md.flatRebuildMs;
  n.gen_q = md.genQueuePending;
  n.mesh_async = md.asyncMeshInFlight;
  n.dirty = md.dirtyChunksPending;
  return n;
}

void WriteJsonl(Session &s, const FrameNumbers &n, const char *kind)
{
  if (!s.Jsonl.is_open())
  {
    return;
  }
  s.Jsonl << "{\"kind\":\"" << kind << "\""
          << ",\"wall_ms\":" << n.wall_ms << ",\"sim_ms\":" << n.sim_ms
          << ",\"swap_wait_ms\":" << n.swap_wait_ms
          << ",\"unaccounted_ms\":" << n.unaccounted_ms
          << ",\"input_ms\":" << n.input_ms
          << ",\"app_update_ms\":" << n.app_update_ms
          << ",\"world_extra_ms\":" << n.world_extra_ms
          << ",\"prepare_frame_ms\":" << n.prepare_frame_ms
          << ",\"post_scene_ms\":" << n.post_scene_ms
          << ",\"gui_overlay_ms\":" << n.gui_overlay_ms
          << ",\"residual_ms\":" << n.residual_ms
          << ",\"fluid_map_cpu_ms\":" << n.fluid_map_cpu_ms
          << ",\"fluid_map_gpu_ms\":" << n.fluid_map_gpu_ms
          << ",\"fluid_map_dirty\":" << n.fluid_map_dirty
          << ",\"fluid_map_full_rebuild\":" << n.fluid_map_full_rebuild
          << ",\"commit_apply_ms\":" << n.commit_apply_ms
          << ",\"commit_seal_ms\":" << n.commit_seal_ms
          << ",\"streamer_update_ms\":" << n.streamer_update_ms
          << ",\"async_io_ms\":" << n.async_io_ms
          << ",\"relight_drain_ms\":" << n.relight_drain_ms
          << ",\"mesh_sync_ms\":" << n.mesh_sync_ms
          << ",\"mesh_snapshot_ms\":" << n.mesh_snapshot_ms
          << ",\"keep_cols\":" << n.keep_cols
          << ",\"visual_cols\":" << n.visual_cols
          << ",\"idle_prefetch_ms\":" << n.idle_prefetch_ms
          << ",\"prefetch_visual_ops\":" << n.prefetch_visual_ops
          << ",\"prefetch_keep_ops\":" << n.prefetch_keep_ops
          << ",\"gen_backlog_total\":" << n.gen_backlog_total
          << ",\"phys_ms\":" << n.phys_ms << ",\"stream_ms\":" << n.stream_ms
          << ",\"mesh_emerge_ms\":" << n.mesh_emerge_ms
          << ",\"scene_ms\":" << n.scene_ms
          << ",\"view_ms\":" << n.view_ms << ",\"flat_ms\":" << n.flat_ms
          << ",\"gen_q\":" << n.gen_q << ",\"mesh_async\":" << n.mesh_async
          << ",\"dirty\":" << n.dirty << "}\n";
  s.Jsonl.flush();
}

void LogLine(const FrameNumbers &n, const char *kind, int frames,
             double max_wall)
{
  LOG(INFO) << "[Perf] kind=" << kind << " wall_ms=" << n.wall_ms
            << " sim_ms=" << n.sim_ms << " swap_wait_ms=" << n.swap_wait_ms
            << " unaccounted_ms=" << n.unaccounted_ms
            << " input_ms=" << n.input_ms
            << " app_update_ms=" << n.app_update_ms
            << " world_extra_ms=" << n.world_extra_ms
            << " prepare_frame_ms=" << n.prepare_frame_ms
            << " post_scene_ms=" << n.post_scene_ms
            << " gui_overlay_ms=" << n.gui_overlay_ms
            << " residual_ms=" << n.residual_ms
            << " fluid_map_cpu_ms=" << n.fluid_map_cpu_ms
            << " fluid_map_gpu_ms=" << n.fluid_map_gpu_ms
            << " fluid_map_dirty=" << n.fluid_map_dirty
            << " fluid_full=" << n.fluid_map_full_rebuild
            << " commit_apply_ms=" << n.commit_apply_ms
            << " commit_seal_ms=" << n.commit_seal_ms
            << " streamer_update_ms=" << n.streamer_update_ms
            << " async_io_ms=" << n.async_io_ms
            << " relight_drain_ms=" << n.relight_drain_ms
            << " mesh_sync_ms=" << n.mesh_sync_ms
            << " mesh_snapshot_ms=" << n.mesh_snapshot_ms
            << " phys_ms=" << n.phys_ms << " stream_ms=" << n.stream_ms
            << " mesh_emerge_ms=" << n.mesh_emerge_ms
            << " scene_ms=" << n.scene_ms << " GenQ=" << n.gen_q
            << " MeshAsync=" << n.mesh_async << " Dirty=" << n.dirty
            << " frames=" << frames << " max_wall_ms=" << max_wall;
}

void Accumulate(Session &s, const FrameNumbers &n)
{
  s.AccumWallMs += n.wall_ms;
  s.AccumSimMs += n.sim_ms;
  s.AccumSwapMs += n.swap_wait_ms;
  s.AccumUnaccountedMs += n.unaccounted_ms;
  s.AccumInputMs += n.input_ms;
  s.AccumAppUpdateMs += n.app_update_ms;
  s.AccumWorldExtraMs += n.world_extra_ms;
  s.AccumPrepareMs += n.prepare_frame_ms;
  s.AccumPostSceneMs += n.post_scene_ms;
  s.AccumGuiMs += n.gui_overlay_ms;
  s.AccumResidualMs += n.residual_ms;
  s.AccumFluidCpuMs += n.fluid_map_cpu_ms;
  s.AccumFluidGpuMs += n.fluid_map_gpu_ms;
  s.MaxWallMs = (std::max)(s.MaxWallMs, n.wall_ms);
  ++s.FrameCount;
}

FrameNumbers AverageFromSession(Session &s, const FrameNumbers &last)
{
  FrameNumbers avg = last;
  const double inv = 1.0 / static_cast<double>(s.FrameCount);
  avg.wall_ms = s.AccumWallMs * inv;
  avg.sim_ms = s.AccumSimMs * inv;
  avg.swap_wait_ms = s.AccumSwapMs * inv;
  avg.unaccounted_ms = s.AccumUnaccountedMs * inv;
  avg.input_ms = s.AccumInputMs * inv;
  avg.app_update_ms = s.AccumAppUpdateMs * inv;
  avg.world_extra_ms = s.AccumWorldExtraMs * inv;
  avg.prepare_frame_ms = s.AccumPrepareMs * inv;
  avg.post_scene_ms = s.AccumPostSceneMs * inv;
  avg.gui_overlay_ms = s.AccumGuiMs * inv;
  avg.residual_ms = s.AccumResidualMs * inv;
  avg.fluid_map_cpu_ms = s.AccumFluidCpuMs * inv;
  avg.fluid_map_gpu_ms = s.AccumFluidGpuMs * inv;
  return avg;
}

void ResetAccum(Session &s)
{
  s.AccumWallMs = 0.0;
  s.AccumSimMs = 0.0;
  s.AccumSwapMs = 0.0;
  s.AccumUnaccountedMs = 0.0;
  s.AccumInputMs = 0.0;
  s.AccumAppUpdateMs = 0.0;
  s.AccumWorldExtraMs = 0.0;
  s.AccumPrepareMs = 0.0;
  s.AccumPostSceneMs = 0.0;
  s.AccumGuiMs = 0.0;
  s.AccumResidualMs = 0.0;
  s.AccumFluidCpuMs = 0.0;
  s.AccumFluidGpuMs = 0.0;
  s.MaxWallMs = 0.0;
  s.FrameCount = 0;
}

} // namespace

void UFramePerfMonitor::EnsureSession()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  OpenSessionLocked(s);
}

void UFramePerfMonitor::OnInGameFrame(UWorld &world, double swap_wait_ms,
                                      double interval_sec)
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  OpenSessionLocked(s);

  const FrameNumbers n = Compute(world, swap_wait_ms);
  Accumulate(s, n);

  const bool spike = n.wall_ms > 100.0;
  if (spike)
  {
    WriteJsonl(s, n, "spike");
    LogLine(n, "spike", 1, n.wall_ms);
  }

  const double interval = interval_sec > 0.05 ? interval_sec : 2.0;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed =
      std::chrono::duration<double>(now - s.LastEmit).count();
  if (elapsed < interval || s.FrameCount <= 0)
  {
    return;
  }

  const FrameNumbers avg = AverageFromSession(s, n);
  WriteJsonl(s, avg, "period");
  LogLine(avg, "period", s.FrameCount, s.MaxWallMs);
  ResetAccum(s);
  s.LastEmit = now;
}

void UFramePerfMonitor::Shutdown()
{
  Session &s = GetSession();
  std::lock_guard<std::mutex> lock(s.Mutex);
  if (s.FrameCount > 0 && s.Jsonl.is_open())
  {
    FrameNumbers last{};
    const FrameNumbers avg = AverageFromSession(s, last);
    WriteJsonl(s, avg, "shutdown");
    LogLine(avg, "shutdown", s.FrameCount, s.MaxWallMs);
    ResetAccum(s);
  }
  if (s.Jsonl.is_open())
  {
    s.Jsonl.close();
  }
  s.Opened = false;
}

} // namespace cutum
