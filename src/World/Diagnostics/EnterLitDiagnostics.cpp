#include "World/Diagnostics/EnterLitDiagnostics.h"

#include "App/Core.h"
#include "App/Platform/Log.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "glog/logging.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace cutum
{

namespace
{

std::mutex g_session_mutex;
std::ofstream g_jsonl;
bool g_session_open{false};
std::chrono::steady_clock::time_point g_session_start{};
double g_last_heartbeat_elapsed_ms{-1.0};
bool g_profile_logged{false};

struct StepAccumulator
{
  int frame_count{0};
  double drain_mesh_sum{0.0};
  double gate_drain_sum{0.0};
  double lit_pass_sum{0.0};
  double relight_drain_sum{0.0};
  double mesh_emerge_sum{0.0};
  double mesh_immediate_sum{0.0};
  double drain_mesh_max{0.0};
  double gate_drain_max{0.0};
  double lit_pass_max{0.0};
  double relight_drain_max{0.0};
  double mesh_emerge_max{0.0};
  std::vector<double> wall_samples;

  void Add(const EnterWarmupStepSample &s)
  {
    ++frame_count;
    drain_mesh_sum += s.drain_mesh_ms;
    gate_drain_sum += s.gate_drain_ms;
    lit_pass_sum += s.lit_pass_ms;
    relight_drain_sum += s.relight_drain_ms;
    mesh_emerge_sum += s.mesh_emerge_ms;
    mesh_immediate_sum += s.mesh_immediate_ms;
    drain_mesh_max = std::max(drain_mesh_max, s.drain_mesh_ms);
    gate_drain_max = std::max(gate_drain_max, s.gate_drain_ms);
    lit_pass_max = std::max(lit_pass_max, s.lit_pass_ms);
    relight_drain_max = std::max(relight_drain_max, s.relight_drain_ms);
    mesh_emerge_max = std::max(mesh_emerge_max, s.mesh_emerge_ms);
    const double wall =
        s.drain_mesh_ms + s.gate_drain_ms + s.lit_pass_ms;
    wall_samples.push_back(wall);
  }

  double P95Wall() const
  {
    if (wall_samples.empty())
    {
      return 0.0;
    }
    std::vector<double> sorted = wall_samples;
    std::sort(sorted.begin(), sorted.end());
    const size_t idx =
        std::min(sorted.size() - 1,
                 static_cast<size_t>(static_cast<double>(sorted.size()) * 0.95));
    return sorted[idx];
  }
};

StepAccumulator g_steps;

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
  oss << "enter_lit_" << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") << ".jsonl";
  const auto logs_dir = GetExecutableDirectory() / "logs";
  return (logs_dir / oss.str()).string();
}

void WriteJsonlLine(const EnterLitSample &s, const char *kind = nullptr)
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
          << ",\"ring_not_ready\":" << s.ring_not_ready
          << ",\"relight_completed_n\":" << s.relight_completed_n
          << ",\"stage_skip_remesh_pending_light\":"
          << s.stage_skip_remesh_pending_light
          << ",\"relight_fifo_dropped\":" << s.relight_fifo_dropped
          << ",\"top_dirty_cx\":" << s.top_dirty_cx
          << ",\"top_dirty_cz\":" << s.top_dirty_cz
          << ",\"gate_miss_cx\":" << s.gate_miss_cx
          << ",\"gate_miss_cy\":" << s.gate_miss_cy
          << ",\"gate_miss_cz\":" << s.gate_miss_cz
          << ",\"gate_miss_soft_held\":" << s.gate_miss_soft_held
          << ",\"gate_miss_defer\":" << s.gate_miss_defer
          << ",\"gate_miss_inflight\":" << s.gate_miss_inflight
          << ",\"gate_miss_has_greedy\":" << s.gate_miss_has_greedy
          << ",\"gate_miss_drawable\":" << s.gate_miss_drawable
          << ",\"gate_miss_gpu_resident\":" << s.gate_miss_gpu_resident
          << ",\"gate_miss_gpu_quad\":" << s.gate_miss_gpu_quad
          << ",\"remesh_after_apply_n\":" << s.remesh_after_apply_n
          << ",\"stuck_dirty_cx\":" << s.stuck_dirty_cx
          << ",\"stuck_dirty_cy\":" << s.stuck_dirty_cy
          << ",\"stuck_dirty_cz\":" << s.stuck_dirty_cz
          << ",\"suppress_relight_seam\":" << (s.suppress_relight_seam ? 1 : 0)
          << ",\"mark_relit_raa_total\":" << s.mark_relit_raa_total
          << ",\"ring_blocker\":\"" << (s.ring_blocker ? s.ring_blocker : "none")
          << "\""
          << ",\"raa_commit_mark_dirty_n\":" << s.raa_commit_mark_dirty_n
          << ",\"markdirty_to_raa_n\":" << s.markdirty_to_raa_n
          << ",\"gpu_kick_n\":" << s.gpu_kick_n
          << ",\"gpu_finish_n\":" << s.gpu_finish_n
          << ",\"mark_relit_prefer_kick_n\":" << s.mark_relit_prefer_kick_n
          << ",\"dirty_schedule_skip_inflight_n\":"
          << s.dirty_schedule_skip_inflight_n
          << ",\"pending_gpu_global\":" << s.pending_gpu_global
          << ",\"enter_lit_quiesce\":" << (s.enter_lit_quiesce ? 1 : 0)
          << ",\"dirty_n\":" << s.dirty_n
          << ",\"stuck_has_chunk\":" << s.stuck_has_chunk
          << ",\"stuck_has_drawable\":" << s.stuck_has_drawable
          << ",\"visibility_debt\":" << s.visibility_debt
          << ",\"dark_face_near_n\":" << s.dark_face_near_n
          << ",\"dark_face_void_near_n\":" << s.dark_face_void_near_n
          << ",\"enter_terminal_held_n\":" << s.enter_terminal_held_n
          << ",\"gate_done_n\":" << s.gate_done_n
          << ",\"enter_phantom_dirty_pruned_n\":"
          << s.enter_phantom_dirty_pruned_n
          << ",\"underfeet_present_ready\":" << s.underfeet_present_ready
          << ",\"spawn_mesh_ring_ready\":" << s.spawn_mesh_ring_ready;
  if (kind != nullptr)
  {
    g_jsonl << ",\"kind\":\"" << kind << "\"";
  }
  g_jsonl << "}\n";
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
            << " visual_warmup=" << (sample.mesh_visual_warmup ? 1 : 0)
            << " ring=" << sample.ring_not_ready
            << " relight_completed=" << sample.relight_completed_n;
  CubatariumFlushLogs();
}

} // namespace

void UEnterLitDiagnostics::BeginSession()
{
  std::lock_guard<std::mutex> lock(g_session_mutex);
  EndSession();
  g_session_start = std::chrono::steady_clock::now();
  g_last_heartbeat_elapsed_ms = -1.0;
  g_profile_logged = false;
  g_steps = {};
  std::error_code ec;
  std::filesystem::create_directories(GetExecutableDirectory() / "logs", ec);
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
  g_profile_logged = false;
  g_steps = {};
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
  out.ring_not_ready = world.CountPostLoadRingNotReady();
  const auto &phys = world.GetPhysicsTelemetry();
  out.relight_completed_n = phys.RelightCompletedN;
  out.stage_skip_remesh_pending_light = phys.StageSkipRemeshPendingLight;
  out.relight_fifo_dropped = phys.RelightFifoDropped;
  out.top_dirty_cx = phys.MissCx;
  out.top_dirty_cz = phys.MissCz;
  const UWorldMeshService &mesh = world.GetMeshService();
  glm::ivec3 gate_miss{};
  if (world.FindFirstSpawnRingMissingGreedy(gate_miss))
  {
    out.gate_miss_cx = gate_miss.x;
    out.gate_miss_cy = gate_miss.y;
    out.gate_miss_cz = gate_miss.z;
    out.gate_miss_soft_held = mesh.IsSoftDeferHeld(gate_miss) ? 1 : 0;
    out.gate_miss_defer =
        mesh.GetCache().IsDeferMeshUntilLit(gate_miss) ? 1 : 0;
    out.gate_miss_inflight = mesh.HasInflightMeshBuild(gate_miss) ? 1 : 0;
    out.gate_miss_has_greedy = mesh.HasGreedyMesh(gate_miss) ? 1 : 0;
    out.gate_miss_drawable = mesh.HasDrawableGreedyMesh(gate_miss) ? 1 : 0;
    const UChunkMeshCache &cache = mesh.GetCache();
    out.gate_miss_gpu_resident = cache.QueryGreedyGpuResident(gate_miss) ? 1 : 0;
    out.gate_miss_gpu_quad = cache.QueryGreedyGpuQuadCount(gate_miss);
  }
  out.remesh_after_apply_n = static_cast<int>(mesh.GetRemeshAfterApplyCount());
  const glm::ivec3 focus = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(focus);
  glm::ivec3 stuck{};
  if (mesh.FindFirstDirtyInHorizontalRadius(focus_chunk, 4, stuck))
  {
    out.stuck_dirty_cx = stuck.x;
    out.stuck_dirty_cy = stuck.y;
    out.stuck_dirty_cz = stuck.z;
    out.stuck_has_chunk =
        world.GetBlockWorld().GetChunkManager().HasChunk(stuck) ? 1 : 0;
    out.stuck_has_drawable = mesh.HasDrawableGreedyMesh(stuck) ? 1 : 0;
  }
  out.suppress_relight_seam = world.IsSuppressRelightSeamDirty();
  out.mark_relit_raa_total = phys.MarkRelitRemeshAfterApplyN;
  out.ring_blocker = EnterWarmupRingBlockerLabel(
      out.mesh_dirty, out.mesh_gpu_pending_near, out.mesh_async_pending,
      out.mesh_missing_greedy);
  out.raa_commit_mark_dirty_n = mesh.GetCache().GetRaaCommitMarkDirtyCount();
  out.markdirty_to_raa_n = mesh.GetCache().GetMarkDirtyToRaaCount();
  out.gpu_kick_n = mesh.GetLastGpuKickN();
  out.gpu_finish_n = mesh.GetLastGpuFinishN();
  out.mark_relit_prefer_kick_n = phys.MarkRelitPreferKickN;
  out.dirty_schedule_skip_inflight_n =
      mesh.GetCache().GetDirtyScheduleSkipInflightCount();
  out.pending_gpu_global =
      static_cast<int>(mesh.GetPendingGpuAppliesCount());
  out.enter_lit_quiesce =
      world.IsEnterLitQuiesceLatched() || mesh.IsEnterLitQuiesce();
  out.dirty_n = static_cast<int>(mesh.GetDirtyCount());
  out.visibility_debt = world.CountEnterVisibilityDebt();
  out.enter_terminal_held_n =
      static_cast<int>(mesh.GetEnterTerminalHeldCount());
  out.gate_done_n = static_cast<int>(mesh.GetEnterGateDoneColumnCount());
  out.enter_phantom_dirty_pruned_n =
      static_cast<int>(mesh.GetEnterPhantomDirtyPrunedTotal());
  out.underfeet_present_ready = world.IsEnterUnderfeetPresentReady() ? 1 : 0;
  out.spawn_mesh_ring_ready = world.IsSpawnMeshRingReady() ? 1 : 0;
  // Era48: sample void-dark during enter so IsEnterVisibilityReady sees telem.
  {
    UChunkMeshCache::DarkFaceHit hit{};
    int near_n = 0;
    int stale_n = 0;
    int void_n = 0;
    glm::vec3 cam_pos(static_cast<float>(focus.x) + 0.5f,
                      static_cast<float>(focus.y) + 1.5f,
                      static_cast<float>(focus.z) + 0.5f);
    if (const auto camera = world.GetCurrentUserCamera())
    {
      cam_pos = camera->GetPosition();
    }
    if (mesh.GetCache().FindNearestDarkFaceNear(
            cam_pos, /*max_dist=*/24.0f, /*chunk_radius=*/2, hit, &near_n,
            &world.GetBlockWorld(), &stale_n, &void_n))
    {
      out.dark_face_near_n = near_n;
      out.dark_face_void_near_n = void_n;
      auto &mut = world.GetPhysicsTelemetryMutable();
      mut.DarkFaceNearN = near_n;
      mut.DarkFaceVoidNearN = void_n;
      mut.DarkFaceStaleNearN = stale_n;
    }
    else
    {
      out.dark_face_near_n = near_n;
      out.dark_face_void_near_n = void_n;
      auto &mut = world.GetPhysicsTelemetryMutable();
      mut.DarkFaceNearN = near_n;
      mut.DarkFaceVoidNearN = void_n;
      mut.DarkFaceStaleNearN = stale_n;
    }
  }
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
            << " fifo=" << sample.fifo_n << " inflight=" << sample.inflight
            << " debt=" << sample.snapshot_debt
            << " mesh_dirty=" << (sample.mesh_dirty ? 1 : 0)
            << " mesh_missing=" << (sample.mesh_missing_greedy ? 1 : 0)
            << " gpu_pending=" << sample.mesh_gpu_pending_near
            << " mesh_async=" << (sample.mesh_async_pending ? 1 : 0)
            << " visual_warmup=" << (sample.mesh_visual_warmup ? 1 : 0)
            << " ring=" << sample.ring_not_ready
            << " relight_completed=" << sample.relight_completed_n
            << " stage_skip_remesh=" << sample.stage_skip_remesh_pending_light
            << " suppress_relight_seam="
            << (sample.suppress_relight_seam ? 1 : 0)
            << " remesh_after_apply_n=" << sample.remesh_after_apply_n
            << " mark_relit_raa_total=" << sample.mark_relit_raa_total
            << " ring_blocker="
            << (sample.ring_blocker ? sample.ring_blocker : "none")
            << " raa_commit_md=" << sample.raa_commit_mark_dirty_n
            << " md_to_raa=" << sample.markdirty_to_raa_n
            << " gpu_kick=" << sample.gpu_kick_n
            << " gpu_finish=" << sample.gpu_finish_n
            << " mark_relit_pk=" << sample.mark_relit_prefer_kick_n
            << " dirty_skip_inflight=" << sample.dirty_schedule_skip_inflight_n
            << " pending_gpu_global=" << sample.pending_gpu_global
            << " top_dirty=(" << sample.top_dirty_cx << ","
            << sample.top_dirty_cz << ")"
            << " stuck_dirty=(" << sample.stuck_dirty_cx << ","
            << sample.stuck_dirty_cy << "," << sample.stuck_dirty_cz << ")";
  CubatariumFlushLogs();
  std::lock_guard<std::mutex> lock(g_session_mutex);
  if (g_session_open)
  {
    WriteJsonlLine(sample, "heartbeat");
  }
}

void UEnterLitDiagnostics::RecordFrameSteps(const EnterWarmupStepSample &steps)
{
  std::lock_guard<std::mutex> lock(g_session_mutex);
  g_steps.Add(steps);
}

void UEnterLitDiagnostics::MaybeLogProfileSummary(const EnterLitSample &sample)
{
  std::lock_guard<std::mutex> lock(g_session_mutex);
  if (g_profile_logged || g_steps.frame_count <= 0)
  {
    return;
  }
  g_profile_logged = true;
  const int n = g_steps.frame_count;
  const double avg_drain = g_steps.drain_mesh_sum / n;
  const double avg_gate = g_steps.gate_drain_sum / n;
  const double avg_lit = g_steps.lit_pass_sum / n;
  const double avg_relight = g_steps.relight_drain_sum / n;
  const double avg_emerge = g_steps.mesh_emerge_sum / n;
  const double wall_avg =
      (g_steps.drain_mesh_sum + g_steps.gate_drain_sum + g_steps.lit_pass_sum) /
      n;
  const double p95 = g_steps.P95Wall();
  double dominant = avg_gate;
  const char *dominant_name = "gate_drain";
  if (avg_drain > dominant)
  {
    dominant = avg_drain;
    dominant_name = "drain_mesh";
  }
  if (avg_lit > dominant)
  {
    dominant = avg_lit;
    dominant_name = "lit_pass";
  }
  const double dominant_pct =
      wall_avg > 0.0 ? (dominant / wall_avg) * 100.0 : 0.0;
  LOG(INFO) << "[EnterWarmup] profile frames=" << n
            << " elapsed_ms=" << sample.elapsed_ms
            << " drain_mesh_avg=" << avg_drain << " max=" << g_steps.drain_mesh_max
            << " gate_drain_avg=" << avg_gate << " max=" << g_steps.gate_drain_max
            << " lit_pass_avg=" << avg_lit << " max=" << g_steps.lit_pass_max
            << " relight_drain_avg=" << avg_relight
            << " max=" << g_steps.relight_drain_max
            << " mesh_emerge_avg=" << avg_emerge
            << " max=" << g_steps.mesh_emerge_max
            << " wall_avg=" << wall_avg << " wall_p95=" << p95
            << " dominant=" << dominant_name << " pct=" << dominant_pct
            << " fifo=" << sample.fifo_n << " gpu_pending="
            << sample.mesh_gpu_pending_near << " ring=" << sample.ring_not_ready;
  CubatariumFlushLogs();
  if (g_session_open)
  {
    WriteJsonlLine(sample, "profile");
  }
}

} // namespace cutum
