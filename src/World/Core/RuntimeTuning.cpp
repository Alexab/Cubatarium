#include "World/Core/RuntimeTuning.h"

#include "World/Physics/FluidTuning.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{

URuntimeTuning &URuntimeTuning::Get()
{
  static URuntimeTuning instance;
  return instance;
}

void URuntimeTuning::ResetToDefaults()
{
  Get() = URuntimeTuning{};
}

void URuntimeTuning::ApplyMemoryTier(const char *tier)
{
  if (!tier || !*tier)
  {
    return;
  }
  URuntimeTuning &t = Get();
  // Compare case-insensitive first letter + known names.
  const bool low = (tier[0] == 'l' || tier[0] == 'L');
  const bool high = (tier[0] == 'h' || tier[0] == 'H');
  if (low)
  {
    t.MemoryBudgetMb = 1024;
    t.MemorySoftMb = 768;
    t.MemoryExpandKeepMb = 512;
    t.GpuVertexPoolReserveMb = 32;
    t.GpuVertexPoolMaxMb = 128;
    t.MaxKeepPrefetchMargin = 2;
    t.MemoryExpandMaxRd = 5;
    t.DirtySoftCap = 800;
    t.DirtyThrashSoftCap = 240;
    t.MaxResidentChunks = 1024;
  }
  else if (high)
  {
    t.MemoryBudgetMb = 3072;
    t.MemorySoftMb = 2304;
    t.MemoryExpandKeepMb = 1536;
    t.GpuVertexPoolReserveMb = 128;
    t.GpuVertexPoolMaxMb = 512;
    t.MaxKeepPrefetchMargin = 6;
    t.MemoryExpandMaxRd = 8;
    t.DirtySoftCap = 2000;
    t.DirtyThrashSoftCap = 480;
    t.MaxResidentChunks = 4096;
  }
  else
  {
    // med (default)
    t.MemoryBudgetMb = 1536;
    t.MemorySoftMb = 1152;
    t.MemoryExpandKeepMb = 768;
    t.GpuVertexPoolReserveMb = 64;
    t.GpuVertexPoolMaxMb = 256;
    t.MaxKeepPrefetchMargin = 4;
    t.MemoryExpandMaxRd = 6;
    t.DirtySoftCap = 1200;
    t.DirtyThrashSoftCap = 280;
    t.MaxResidentChunks = 0; // auto Keep footprint
  }
}

void URuntimeTuning::LoadStreamingTuneFile(const char *path)
{
  if (!path || !*path)
  {
    return;
  }
  static std::string last_path;
  static std::filesystem::file_time_type last_mtime{};
  static bool have_mtime = false;
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  if (ec)
  {
    return;
  }
  if (have_mtime && path == last_path && mtime == last_mtime)
  {
    return;
  }
  std::ifstream in(path);
  if (!in)
  {
    return;
  }
  nlohmann::json j;
  try
  {
    in >> j;
  }
  catch (...)
  {
    return;
  }
  URuntimeTuning &t = Get();
  if (j.contains("mesh_forward_bias_k"))
  {
    t.MeshForwardBiasK = j.value("mesh_forward_bias_k", t.MeshForwardBiasK);
  }
  if (j.contains("relight_inflight_mult_high"))
  {
    t.RelightInflightMultHigh =
        j.value("relight_inflight_mult_high", t.RelightInflightMultHigh);
  }
  if (j.contains("relight_inflight_mult_holes"))
  {
    t.RelightInflightMultHoles =
        j.value("relight_inflight_mult_holes", t.RelightInflightMultHoles);
  }
  if (j.contains("mesh_fly_cap_yellow"))
  {
    t.MeshFlyCapYellow = j.value("mesh_fly_cap_yellow", t.MeshFlyCapYellow);
  }
  if (j.contains("mesh_fly_cap_red"))
  {
    t.MeshFlyCapRed = j.value("mesh_fly_cap_red", t.MeshFlyCapRed);
  }
  if (j.contains("recover_n_boost"))
  {
    t.RecoverNBoost = j.value("recover_n_boost", t.RecoverNBoost);
  }
  if (j.contains("memory_tier") && j["memory_tier"].is_string())
  {
    ApplyMemoryTier(j["memory_tier"].get_ref<const std::string &>().c_str());
  }
  if (j.contains("memory_budget_mb"))
  {
    t.MemoryBudgetMb = j.value("memory_budget_mb", t.MemoryBudgetMb);
  }
  if (j.contains("memory_soft_mb"))
  {
    t.MemorySoftMb = j.value("memory_soft_mb", t.MemorySoftMb);
  }
  if (j.contains("memory_expand_keep_mb"))
  {
    t.MemoryExpandKeepMb =
        j.value("memory_expand_keep_mb", t.MemoryExpandKeepMb);
  }
  if (j.contains("mesh_completed_slots"))
  {
    t.MeshCompletedSlots =
        j.value("mesh_completed_slots", t.MeshCompletedSlots);
  }
  if (j.contains("relight_completed_slots"))
  {
    t.RelightCompletedSlots =
        j.value("relight_completed_slots", t.RelightCompletedSlots);
  }
  if (j.contains("dirty_soft_cap"))
  {
    t.DirtySoftCap = j.value("dirty_soft_cap", t.DirtySoftCap);
  }
  if (j.contains("dirty_thrash_soft_cap"))
  {
    t.DirtyThrashSoftCap =
        j.value("dirty_thrash_soft_cap", t.DirtyThrashSoftCap);
  }
  if (j.contains("dirty_thrash_async_min"))
  {
    t.DirtyThrashAsyncMin =
        j.value("dirty_thrash_async_min", t.DirtyThrashAsyncMin);
  }
  if (j.contains("max_resident_chunks"))
  {
    t.MaxResidentChunks = j.value("max_resident_chunks", t.MaxResidentChunks);
  }
  if (j.contains("relight_capture_band_cy"))
  {
    t.RelightCaptureBandCy =
        j.value("relight_capture_band_cy", t.RelightCaptureBandCy);
  }
  if (j.contains("pending_light_soft_cap"))
  {
    t.PendingLightSoftCap =
        j.value("pending_light_soft_cap", t.PendingLightSoftCap);
  }
  if (j.contains("dirty_admit_cap_red"))
  {
    t.DirtyAdmitCapRed = j.value("dirty_admit_cap_red", t.DirtyAdmitCapRed);
  }
  if (j.contains("dirty_admit_cap_yellow"))
  {
    t.DirtyAdmitCapYellow =
        j.value("dirty_admit_cap_yellow", t.DirtyAdmitCapYellow);
  }
  if (j.contains("relight_fifo_admit_frac"))
  {
    t.RelightFifoAdmitFrac =
        j.value("relight_fifo_admit_frac", t.RelightFifoAdmitFrac);
  }
  if (j.contains("miss_reserved_ms"))
  {
    t.MissReservedMs = j.value("miss_reserved_ms", t.MissReservedMs);
  }
  if (j.contains("miss_emerge_floor_ms"))
  {
    t.MissEmergeFloorMs =
        j.value("miss_emerge_floor_ms", t.MissEmergeFloorMs);
  }
  if (j.contains("streaming_phase_budget_ms"))
  {
    t.StreamingPhaseBudgetMs =
        j.value("streaming_phase_budget_ms", t.StreamingPhaseBudgetMs);
  }
  if (j.contains("relight_fifo_soft_cap"))
  {
    t.RelightFifoSoftCap =
        j.value("relight_fifo_soft_cap", t.RelightFifoSoftCap);
    t.Era18VbCaptureFloor =
        j.value("era18_vb_capture_floor", t.Era18VbCaptureFloor);
    t.Era18VbBgBudgetFloor =
        j.value("era18_vb_bg_budget_floor", t.Era18VbBgBudgetFloor);
    t.MissFirstFrameBudget =
        j.value("miss_first_frame_budget", t.MissFirstFrameBudget);
  }
  if (j.contains("gpu_vertex_pool_reserve_mb"))
  {
    t.GpuVertexPoolReserveMb =
        j.value("gpu_vertex_pool_reserve_mb", t.GpuVertexPoolReserveMb);
  }
  if (j.contains("gpu_vertex_pool_max_mb"))
  {
    t.GpuVertexPoolMaxMb =
        j.value("gpu_vertex_pool_max_mb", t.GpuVertexPoolMaxMb);
  }
  if (j.contains("max_keep_prefetch_margin"))
  {
    t.MaxKeepPrefetchMargin =
        j.value("max_keep_prefetch_margin", t.MaxKeepPrefetchMargin);
  }
  if (j.contains("memory_expand_max_rd"))
  {
    t.MemoryExpandMaxRd =
        j.value("memory_expand_max_rd", t.MemoryExpandMaxRd);
  }
  if (j.contains("completed_expand_enabled"))
  {
    t.CompletedExpandEnabled =
        j.value("completed_expand_enabled", t.CompletedExpandEnabled);
  }
  // Phase B: Capture / Immediate / fly / hitch / fog timing.
  if (j.contains("capture_drain_moving_ms"))
  {
    t.CaptureDrainMovingMs =
        j.value("capture_drain_moving_ms", t.CaptureDrainMovingMs);
  }
  if (j.contains("capture_drain_idle_ms"))
  {
    t.CaptureDrainIdleMs =
        j.value("capture_drain_idle_ms", t.CaptureDrainIdleMs);
  }
  if (j.contains("capture_drain_holes_moving_ms"))
  {
    t.CaptureDrainHolesMovingMs =
        j.value("capture_drain_holes_moving_ms", t.CaptureDrainHolesMovingMs);
  }
  if (j.contains("capture_drain_holes_idle_ms"))
  {
    t.CaptureDrainHolesIdleMs =
        j.value("capture_drain_holes_idle_ms", t.CaptureDrainHolesIdleMs);
  }
  if (j.contains("capture_drain_high_pending_moving_ms"))
  {
    t.CaptureDrainHighPendingMovingMs = j.value(
        "capture_drain_high_pending_moving_ms", t.CaptureDrainHighPendingMovingMs);
  }
  if (j.contains("capture_drain_high_pending_idle_ms"))
  {
    t.CaptureDrainHighPendingIdleMs = j.value(
        "capture_drain_high_pending_idle_ms", t.CaptureDrainHighPendingIdleMs);
  }
  if (j.contains("capture_hot_frame_mult"))
  {
    t.CaptureHotFrameMult =
        j.value("capture_hot_frame_mult", t.CaptureHotFrameMult);
  }
  if (j.contains("capture_sync_skip_wall_ms"))
  {
    t.CaptureSyncSkipWallMs =
        j.value("capture_sync_skip_wall_ms", t.CaptureSyncSkipWallMs);
  }
  if (j.contains("capture_idle_pending_max_wall_ms"))
  {
    t.CaptureIdlePendingMaxWallMs = j.value(
        "capture_idle_pending_max_wall_ms", t.CaptureIdlePendingMaxWallMs);
  }
  if (j.contains("capture_moving_bg_cap"))
  {
    t.CaptureMovingBgCap =
        j.value("capture_moving_bg_cap", t.CaptureMovingBgCap);
  }
  if (j.contains("immediate_budget_hot_ms"))
  {
    t.ImmediateBudgetHotMs =
        j.value("immediate_budget_hot_ms", t.ImmediateBudgetHotMs);
  }
  if (j.contains("immediate_budget_ok_ms"))
  {
    t.ImmediateBudgetOkMs =
        j.value("immediate_budget_ok_ms", t.ImmediateBudgetOkMs);
  }
  if (j.contains("immediate_hot_wall_ms"))
  {
    t.ImmediateHotWallMs =
        j.value("immediate_hot_wall_ms", t.ImmediateHotWallMs);
  }
  if (j.contains("mesh_fly_wall_hot_ms"))
  {
    t.MeshFlyWallHotMs = j.value("mesh_fly_wall_hot_ms", t.MeshFlyWallHotMs);
  }
  if (j.contains("mesh_fly_wall_mid_ms"))
  {
    t.MeshFlyWallMidMs = j.value("mesh_fly_wall_mid_ms", t.MeshFlyWallMidMs);
  }
  if (j.contains("mesh_fly_cap_wall_hot"))
  {
    t.MeshFlyCapWallHot =
        j.value("mesh_fly_cap_wall_hot", t.MeshFlyCapWallHot);
  }
  if (j.contains("mesh_fly_cap_wall_mid"))
  {
    t.MeshFlyCapWallMid =
        j.value("mesh_fly_cap_wall_mid", t.MeshFlyCapWallMid);
  }
  if (j.contains("mesh_fly_cap_wall_ok"))
  {
    t.MeshFlyCapWallOk = j.value("mesh_fly_cap_wall_ok", t.MeshFlyCapWallOk);
  }
  if (j.contains("mesh_fly_cap_holes_hot"))
  {
    t.MeshFlyCapHolesHot =
        j.value("mesh_fly_cap_holes_hot", t.MeshFlyCapHolesHot);
  }
  if (j.contains("mesh_fly_cap_holes_ok"))
  {
    t.MeshFlyCapHolesOk =
        j.value("mesh_fly_cap_holes_ok", t.MeshFlyCapHolesOk);
  }
  if (j.contains("memory_green_max_wall_ms"))
  {
    t.MemoryGreenMaxWallMs =
        j.value("memory_green_max_wall_ms", t.MemoryGreenMaxWallMs);
  }
  if (j.contains("memory_hitch_capture_wall_ms"))
  {
    t.MemoryHitchCaptureWallMs =
        j.value("memory_hitch_capture_wall_ms", t.MemoryHitchCaptureWallMs);
  }
  if (j.contains("memory_urgent_eval_wall_ms"))
  {
    t.MemoryUrgentEvalWallMs =
        j.value("memory_urgent_eval_wall_ms", t.MemoryUrgentEvalWallMs);
  }
  if (j.contains("fog_pull_in_expand_sec"))
  {
    t.FogPullInExpandSec =
        j.value("fog_pull_in_expand_sec", t.FogPullInExpandSec);
  }
  if (j.contains("fog_pull_in_shrink_sec"))
  {
    t.FogPullInShrinkSec =
        j.value("fog_pull_in_shrink_sec", t.FogPullInShrinkSec);
  }
  if (j.contains("fog_pull_in_severe_wall_ms"))
  {
    t.FogPullInSevereWallMs =
        j.value("fog_pull_in_severe_wall_ms", t.FogPullInSevereWallMs);
  }
  if (j.contains("enter_fov_lit_hard_wall_ms"))
  {
    t.EnterFovLitHardWallMs =
        j.value("enter_fov_lit_hard_wall_ms", t.EnterFovLitHardWallMs);
  }
  if (j.contains("enter_fov_lit_capture_budget"))
  {
    t.EnterFovLitCaptureBudget =
        j.value("enter_fov_lit_capture_budget", t.EnterFovLitCaptureBudget);
  }
  if (j.contains("enter_fov_lit_apply_budget"))
  {
    t.EnterFovLitApplyBudget =
        j.value("enter_fov_lit_apply_budget", t.EnterFovLitApplyBudget);
  }
  if (j.contains("enter_fov_lit_capture_drain_ms"))
  {
    t.EnterFovLitCaptureDrainMs =
        j.value("enter_fov_lit_capture_drain_ms", t.EnterFovLitCaptureDrainMs);
  }
  if (j.contains("enter_fov_lit_inflight_mult"))
  {
    t.EnterFovLitInflightMult =
        j.value("enter_fov_lit_inflight_mult", t.EnterFovLitInflightMult);
  }
  if (j.contains("enter_lit_require_zero"))
  {
    t.EnterLitRequireZero =
        j.value("enter_lit_require_zero", t.EnterLitRequireZero);
  }
  if (j.contains("enter_lit_debt_mode"))
  {
    const std::string mode = j.value("enter_lit_debt_mode", std::string("snapshot_rd"));
    t.EnterLitUseSnapshotDebt = (mode != "live_global");
  }
  if (j.contains("enter_lit_abort_ms"))
  {
    t.EnterLitAbortMs = j.value("enter_lit_abort_ms", t.EnterLitAbortMs);
  }
  if (j.contains("enter_mesh_abort_ms"))
  {
    t.EnterMeshAbortMs = j.value("enter_mesh_abort_ms", t.EnterMeshAbortMs);
  }
  if (j.contains("enter_gate_mesh_drain_iterations"))
  {
    t.EnterGateMeshDrainIterations =
        j.value("enter_gate_mesh_drain_iterations",
                t.EnterGateMeshDrainIterations);
  }
  if (j.contains("enter_force_ingame_ms"))
  {
    t.EnterForceInGameMs =
        j.value("enter_force_ingame_ms", t.EnterForceInGameMs);
  }
  if (j.contains("strict_enter_visual_ready"))
  {
    t.StrictEnterVisualReady =
        j.value("strict_enter_visual_ready", t.StrictEnterVisualReady);
  }
  if (j.contains("fz2_defer_gated"))
  {
    t.Fz2DeferGated = j.value("fz2_defer_gated", t.Fz2DeferGated);
  }
  if (j.contains("fz2_lit_ring_seed"))
  {
    t.Fz2LitRingSeed = j.value("fz2_lit_ring_seed", t.Fz2LitRingSeed);
  }
  last_path = path;
  last_mtime = mtime;
  have_mtime = true;
}

} // namespace cutum
