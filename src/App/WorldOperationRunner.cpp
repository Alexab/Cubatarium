#include "App/WorldOperationRunner.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "App/Core.h"
#include "App/Platform/Log.h"
#include "Core/Progress/ProgressTypes.h"
#include "World/Core/World.h"
#include "glog/logging.h"
#include <chrono>
#include <iostream>
#include <string>

namespace cutum
{

namespace
{

WorldOperationKind KindForRunnerOp(WorldRunnerOp op)
{
  switch (op)
  {
  case WorldRunnerOp::Save:
  case WorldRunnerOp::SaveThenLoad:
  case WorldRunnerOp::SaveThenCreate:
    return WorldOperationKind::Save;
  case WorldRunnerOp::Load:
    return WorldOperationKind::Load;
  case WorldRunnerOp::Create:
    return WorldOperationKind::Create;
  case WorldRunnerOp::EnterGame:
    return WorldOperationKind::EnterGame;
  case WorldRunnerOp::Shutdown:
    return WorldOperationKind::Shutdown;
  }
  return WorldOperationKind::Load;
}

constexpr int kChunkBudgetPerFrame = 16;
constexpr int kEnterGameGpuWarmupMinFrames = 3;
constexpr int kEnterGameGpuWarmupMaxFrames = 24;

} // namespace

UWorldOperationRunner::UWorldOperationRunner(UCore &core, UWorld &world)
    : Core(core), World(world)
{
}

void UWorldOperationRunner::Start(WorldRunnerRequest request)
{
  Request = std::move(request);
  Active = true;
  Success = false;
  Error.clear();
  PendingWorldName.clear();
  SaveBeforeOp = false;
  PendingWorldOp = WorldRunnerOp::Load;
  EnterLoadElapsedMs = 0.0;
  EnterGameColdCreate = false;

  switch (Request.op)
  {
  case WorldRunnerOp::Save:
    CurrentStage = Stage::WorldOperation;
    PendingWorldOp = WorldRunnerOp::Save;
    break;
  case WorldRunnerOp::Load:
    CurrentStage = Stage::WorldOperation;
    PendingWorldOp = WorldRunnerOp::Load;
    Core.PrepareLoadWorld(Request.worldName);
    PendingWorldName = Request.worldName;
    break;
  case WorldRunnerOp::Create:
    CurrentStage = Stage::PrepareCreate;
    PendingWorldOp = WorldRunnerOp::Create;
    break;
  case WorldRunnerOp::SaveThenLoad:
    SaveBeforeOp = true;
    CurrentStage = Stage::WorldOperation;
    PendingWorldOp = WorldRunnerOp::Save;
    PendingWorldName = Request.worldName;
    break;
  case WorldRunnerOp::SaveThenCreate:
    SaveBeforeOp = true;
    CurrentStage = Stage::WorldOperation;
    PendingWorldOp = WorldRunnerOp::Save;
    break;
  case WorldRunnerOp::EnterGame:
    CurrentStage = Stage::EnterGameList;
    break;
  case WorldRunnerOp::Shutdown:
    CurrentStage = Stage::ShutdownQuiesce;
    // Drop any in-flight autosave: TickBudgetedAutosave stops in Loading and
    // would otherwise leave a half-finished coop session for ShutdownSave.
    if (World.HasActiveCooperativeOperation())
    {
      World.CancelCooperativeOperation();
    }
    World.BeginBackgroundQuiesce(ShutdownQuiesceState);
    break;
  }
}

void UWorldOperationRunner::Fail(const std::string &message, IUProgressSink &sink)
{
  Error = message;
  Success = false;
  Active = false;
  CurrentStage = Stage::Failed;
  sink.End(false, message);
  std::cerr << "World operation failed: " << message << std::endl;
}

bool UWorldOperationRunner::TickWorldOp(IUProgressSink &sink, int chunkBudget)
{
  const int budget = std::max(1, chunkBudget);
  switch (PendingWorldOp)
  {
  case WorldRunnerOp::Save:
  {
    const std::string folder = Core.GetActiveWorldFolder().string();
    if (folder.empty())
    {
      Fail("No active world folder to save.", sink);
      return true;
    }
    if (!World.HasActiveCooperativeOperation())
    {
      // SaveThenCreate / SaveThenLoad tear the session down next — do not
      // resume streaming (InitChunkScheduler join) after this preliminary save.
      const bool resume_streaming = !SaveBeforeOp;
      World.BeginCooperativeSave(folder, resume_streaming);
    }
    if (World.TickCooperativeSave(sink, budget))
    {
      return true;
    }
    return false;
  }
  case WorldRunnerOp::Load:
  {
    const std::string folder = Core.GetActiveWorldFolder().string();
    if (folder.empty())
    {
      Fail("No world folder to load.", sink);
      return true;
    }
    if (!World.HasActiveCooperativeOperation())
    {
      World.BeginCooperativeLoad(folder);
    }
    if (World.TickCooperativeLoad(sink, budget))
    {
      return true;
    }
    return false;
  }
  case WorldRunnerOp::Create:
  {
    if (PendingWorldName.empty())
    {
      Fail("World name was not prepared.", sink);
      return true;
    }
    if (!World.HasActiveCooperativeOperation())
    {
      World.SetWorldFolderPath(Core.GetActiveWorldFolder().string());
      World.BeginCooperativeCreate(PendingWorldName);
    }
    if (World.TickCooperativeCreate(sink, budget))
    {
      return true;
    }
    return false;
  }
  default:
    return true;
  }
}

void UWorldOperationRunner::PrepareCreateWorld()
{
  Core.ApplyNewWorldCreationRequest(Request.settings, Request.packs,
                                    Request.view, Request.gameMode,
                                    Request.difficulty);
  PendingWorldName = Core.SetupNewWorldForCreation();
}

void UWorldOperationRunner::AccumulateEnterLoadMs(double frame_ms)
{
  if (!Active || Request.op != WorldRunnerOp::EnterGame)
  {
    return;
  }
  if (frame_ms > 0.0)
  {
    EnterLoadElapsedMs += frame_ms;
  }
}

bool UWorldOperationRunner::EnterVisualCapReached() const
{
  if (Request.op != WorldRunnerOp::EnterGame)
  {
    return false;
  }
  // Soft-ready only: do NOT key off EnterLoadElapsedMs here — that timer
  // includes cooperative terrain load (seconds) and would abort/skip warmup
  // after 200ms with an unfinished world (stuck on "World loaded" 100%).
  return !World.NeedsEnterGameMeshWarmup() && World.IsSpawnMeshRingReady() &&
         World.IsEnterVisibilityReady();
}

bool UWorldOperationRunner::AdvanceEnterGameGpuWarmup(IUProgressSink &sink,
                                                      double frame_ms)
{
  if (CurrentStage != Stage::EnterGameGpuWarmup)
  {
    return true;
  }
  EnterGameGpuWarmupElapsedMs += frame_ms;
  const auto &tune = URuntimeTuning::Get();
  EnterLitSample lit_sample{};
  UEnterLitDiagnostics::Sample(World, EnterGameGpuWarmupElapsedMs, lit_sample);
  const int fov_debt = lit_sample.snapshot_debt;
  if (fov_debt > EnterGameFovLitPeakDebt)
  {
    EnterGameFovLitPeakDebt = fov_debt;
  }
  EnterGameFifoPeak = std::max(EnterGameFifoPeak, lit_sample.fifo_n);
  EnterGameGpuPeak =
      std::max(EnterGameGpuPeak, lit_sample.mesh_gpu_pending_near);
  EnterGameRingPeak = std::max(EnterGameRingPeak, lit_sample.ring_not_ready);
  const int fifo_peak = std::max(1, EnterGameFifoPeak);
  const int gpu_peak = std::max(1, EnterGameGpuPeak);
  const int ring_peak = std::max(1, EnterGameRingPeak);
  const int fov_peak = std::max(1, EnterGameFovLitPeakDebt);

  const bool coop_prepared = World.IsSpawnAreaPreparedByCooperativeLoad();
  const bool ring_ready =
      coop_prepared || World.IsSpawnMeshRingReady();
  const bool mesh_blockers_clear =
      coop_prepared || !World.NeedsEnterGameMeshWarmup();
  const bool fov_ready = coop_prepared || fov_debt <= 0;
  const bool visibility_ready =
      coop_prepared || World.IsEnterVisibilityReady();
  const int visibility_debt = lit_sample.visibility_debt;
  const bool soft_ready =
      mesh_blockers_clear && fov_ready && ring_ready && visibility_ready;

  const bool cap_reached = ShouldForceEnterVisualCap(
      EnterGameGpuWarmupElapsedMs, soft_ready, EnterGameColdCreate,
      tune.EnterFovLitHardWallMs, tune.EnterLitRequireZero);
  if (!cap_reached && (fov_debt > 0 || visibility_debt > 0) &&
      EnterGameGpuWarmupElapsedMs >=
          static_cast<double>(tune.EnterFovLitHardWallMs) &&
      !EnterGameLitWarnLogged)
  {
    EnterGameLitWarnLogged = true;
    std::cerr << "[Era42/48] enter lit/visibility still draining past warn wall ("
              << EnterGameGpuWarmupElapsedMs << "ms, lit=" << fov_debt
              << " vis=" << visibility_debt << ")\n";
  }
  // Era48: never EndEnterLitGate on lit abort while visibility debt remains —
  // keep unified drain (log only).
  if (!EnterGameForceLitAbort && tune.EnterLitRequireZero && fov_debt > 0 &&
      tune.EnterLitAbortMs > 0 &&
      EnterGameGpuWarmupElapsedMs >= static_cast<double>(tune.EnterLitAbortMs) &&
      World.IsEnterLitGateActive())
  {
    EnterGameForceLitAbort = true;
    std::cerr << "[Era43/48] enter lit abort wall after "
              << EnterGameGpuWarmupElapsedMs << "ms, residual_lit=" << fov_debt
              << " vis=" << visibility_debt
              << " (continuing drain; no force InGame)\n";
  }
  if (!EnterGameAbortDrainMode &&
      ShouldForceEnterMeshAbort(fov_debt, ring_ready, EnterGameGpuWarmupElapsedMs,
                                tune.EnterMeshAbortMs))
  {
    EnterGameForceMeshAbort = true;
    EnterGameAbortDrainMode = true;
    if (!EnterGameAbortDrainLogged)
    {
      EnterGameAbortDrainLogged = true;
      LOG(INFO) << "[EnterWarmup] abort_drain elapsed_ms="
                << EnterGameGpuWarmupElapsedMs << " dirty="
                << (lit_sample.mesh_dirty ? 1 : 0)
                << " missing=" << (lit_sample.mesh_missing_greedy ? 1 : 0)
                << " gpu_pending=" << lit_sample.mesh_gpu_pending_near
                << " async=" << (lit_sample.mesh_async_pending ? 1 : 0)
                << " ring_not_ready=" << lit_sample.ring_not_ready
                << " fifo=" << lit_sample.fifo_n
                << " visibility_debt=" << visibility_debt;
      CubatariumFlushLogs();
    }
  }

  const float raw_prog = EnterGpuWarmupProgressFraction(
      lit_sample.fifo_n, fifo_peak, lit_sample.mesh_gpu_pending_near, gpu_peak,
      lit_sample.ring_not_ready, ring_peak,
      fov_debt + visibility_debt, std::max(fov_peak, 1));
  const float enter_prog =
      EnterGpuWarmupMonotonicProgress(raw_prog, EnterGameDisplayProgress);
  // Era48: hold bar under 100% until visibility ready.
  const float capped_prog =
      visibility_ready ? enter_prog : std::min(enter_prog, 0.99f);
  const float frac = 0.93f + 0.07f * capped_prog;
  const std::string status = BuildEnterWarmupStatus(
      lit_sample, fov_debt, ring_ready, EnterGameAbortDrainMode,
      EnterGameGpuWarmupElapsedMs, tune.EnterFovLitHardWallMs, visibility_debt);
  sink.Report("prepare_view", frac, status);

  if (EnterGameGpuWarmupFramesLeft > 0)
  {
    --EnterGameGpuWarmupFramesLeft;
  }
  const int frame_index =
      kEnterGameGpuWarmupMaxFrames - EnterGameGpuWarmupFramesLeft;
  UEnterLitDiagnostics::MaybeLog(lit_sample, frame_index);
  UEnterLitDiagnostics::MaybeLogHeartbeat(lit_sample, 2000.0);
  const bool min_frames_done =
      frame_index >= kEnterGameGpuWarmupMinFrames;
  if (!mesh_blockers_clear || !ring_ready)
  {
    World.SetEnterGameWarmupMissingGreedy(lit_sample.ring_not_ready);
  }
  if (ring_ready && mesh_blockers_clear && fov_ready && visibility_ready)
  {
    UEnterLitDiagnostics::MaybeLogProfileSummary(lit_sample);
  }

  const bool enter_ready = IsEnterGpuWarmupReady(
      ring_ready, fov_ready ? 0 : fov_debt, mesh_blockers_clear, min_frames_done,
      visibility_ready);
  const bool force_ingame =
      EnterGameAbortDrainMode &&
      ShouldForceEnterInGameAfterAbortDrain(EnterGameGpuWarmupElapsedMs,
                                            tune.EnterForceInGameMs);
  if (force_ingame && !enter_ready && !EnterGameForceInGameLogged)
  {
    EnterGameForceInGameLogged = true;
    LOG(WARNING) << "[EnterWarmup] force_ingame wall elapsed_ms="
                 << EnterGameGpuWarmupElapsedMs << " ring_ready="
                 << (ring_ready ? 1 : 0) << " mesh_dirty="
                 << (lit_sample.mesh_dirty ? 1 : 0) << " gpu_pending="
                 << lit_sample.mesh_gpu_pending_near << " ring="
                 << lit_sample.ring_not_ready << " fifo=" << lit_sample.fifo_n
                 << " visibility_debt=" << visibility_debt
                 << " (Era48: ignored until visibility ready)";
    CubatariumFlushLogs();
  }

  // Era48/49: InGame only when visibility ready — abort/force/cap do not bypass.
  // visibility_ready = unready==0 ∧ void≤200 ∧ stale==0 (StrictEnterVisualReady).
  if (!enter_ready)
  {
    return false;
  }
  if (World.IsEnterLitGateActive())
  {
    World.EndEnterLitGate();
    UEnterLitDiagnostics::EndSession();
  }
  CurrentStage = Stage::EnterGameFinalize;
  return false;
}

bool UWorldOperationRunner::Tick(IUProgressSink &sink, int chunkBudgetPerFrame)
{
  if (!Active || CurrentStage == Stage::Done || CurrentStage == Stage::Failed)
  {
    return true;
  }

  const int budget = chunkBudgetPerFrame > 0 ? chunkBudgetPerFrame : kChunkBudgetPerFrame;

  switch (CurrentStage)
  {
  case Stage::PrepareCreate:
    sink.Begin(WorldOperationKind::Create);
    PrepareCreateWorld();
    CurrentStage = Stage::WorldOperation;
    sink.Report("prepare", 0.f, "Preparing new world...");
    return false;

  case Stage::PreReplaceTerrain:
    sink.Report("teardown", 0.05f, "Clearing previous world...");
    World.AbandonTerrainForWorldReplace();
    if (Request.op == WorldRunnerOp::SaveThenLoad)
    {
      Core.PrepareLoadWorld(PendingWorldName);
      PendingWorldOp = WorldRunnerOp::Load;
      CurrentStage = Stage::WorldOperation;
      sink.Begin(WorldOperationKind::Load);
      sink.Report("load", 0.f, "Loading world...");
    }
    else
    {
      PendingWorldOp = WorldRunnerOp::Create;
      CurrentStage = Stage::PrepareCreate;
    }
    return false;

  case Stage::WorldOperation:
  {
    const WorldOperationKind kind =
        PendingWorldOp == WorldRunnerOp::Save &&
                (Request.op == WorldRunnerOp::SaveThenLoad ||
                 Request.op == WorldRunnerOp::SaveThenCreate)
            ? WorldOperationKind::Save
            : KindForRunnerOp(PendingWorldOp);
    if (!World.HasActiveCooperativeOperation())
    {
      sink.Begin(kind);
    }
    if (!TickWorldOp(sink, budget))
    {
      return false;
    }

    if (PendingWorldOp == WorldRunnerOp::Save &&
        (Request.op == WorldRunnerOp::SaveThenLoad ||
         Request.op == WorldRunnerOp::SaveThenCreate))
    {
      SaveBeforeOp = false;
      CurrentStage = Stage::PreReplaceTerrain;
      sink.Report("teardown", 0.f, "Clearing previous world...");
      return false;
    }

    if (PendingWorldOp == WorldRunnerOp::Load)
    {
      CurrentStage = Stage::PostLoadUsers;
      return false;
    }
    if (PendingWorldOp == WorldRunnerOp::Create)
    {
      CurrentStage = Stage::PostCreateUsers;
      return false;
    }
    if (Request.op == WorldRunnerOp::Save)
    {
      Success = true;
      Active = false;
      CurrentStage = Stage::Done;
      sink.End(true);
      return true;
    }
    break;
  }

  case Stage::PostLoadUsers:
    Core.FinalizeLoadedWorld();
    if (Request.saveConfigAfter)
    {
      Core.SaveConfigFile();
    }
    if (Request.op == WorldRunnerOp::EnterGame)
    {
      if (EnterVisualCapReached())
      {
        CurrentStage = Stage::EnterGameFinalize;
        return false;
      }
      CurrentStage = Stage::EnterGameGpuWarmup;
      EnterGameGpuWarmupFramesLeft = kEnterGameGpuWarmupMaxFrames;
      EnterGameGpuWarmupElapsedMs = 0.0;
      EnterGameFovLitPeakDebt = 0;
      EnterGameLitWarnLogged = false;
      EnterGameForceLitAbort = false;
      EnterGameForceMeshAbort = false;
      EnterGameAbortDrainMode = false;
      EnterGameAbortDrainLogged = false;
      EnterGameForceInGameLogged = false;
      EnterGameFifoPeak = 0;
      EnterGameGpuPeak = 0;
      EnterGameRingPeak = 0;
      EnterGameDisplayProgress = 0.0f;
      EnterGameColdCreate = false;
      return false;
    }
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  case Stage::PostCreateUsers:
    Core.ApplyDefaultEnvironmentToWorld();
    if (World.GetCurrentUser() == nullptr)
    {
      World.GenerateUsers();
    }
    if (auto merge_registry = Core.GetBlockMergeRegistry())
    {
      World.SetCatalogFingerprint(merge_registry->ComputeCatalogFingerprint());
    }
    WarnIfSpawnSkylightMissing(World, "before create save");
    CurrentStage = Stage::PostCreateSave;
    PendingWorldOp = WorldRunnerOp::Save;
    sink.Begin(WorldOperationKind::Save);
    sink.Report("save", 0.f, "Saving new world...");
    return false;

  case Stage::PostCreateSave:
    if (!TickWorldOp(sink, budget))
    {
      return false;
    }
    Core.RefreshWorldListAfterSave();
    World.RefreshPersistedTerrainAfterSave();
    if (Request.saveConfigAfter)
    {
      Core.SaveConfigFile();
    }
    if (Request.op == WorldRunnerOp::EnterGame)
    {
      if (EnterVisualCapReached())
      {
        CurrentStage = Stage::EnterGameFinalize;
        return false;
      }
      CurrentStage = Stage::EnterGameGpuWarmup;
      EnterGameGpuWarmupFramesLeft = kEnterGameGpuWarmupMaxFrames;
      EnterGameGpuWarmupElapsedMs = 0.0;
      EnterGameFovLitPeakDebt = 0;
      EnterGameLitWarnLogged = false;
      EnterGameColdCreate = true;
      return false;
    }
    // Create + enterGameAfter: PrepareView already settled LitDrawable ring;
    // EnterGameAfterWorldChange runs from Application after Done.
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  case Stage::EnterGameGpuWarmup:
    return false;

  case Stage::EnterGameFinalize:
    if (World.IsEnterLitGateActive())
    {
      World.EndEnterLitGate();
      UEnterLitDiagnostics::EndSession();
    }
    Core.FinalizeEnterGameSession();
    Core.SaveConfigFile();
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  case Stage::EnterGameList:
    sink.Begin(WorldOperationKind::EnterGame);
    Core.PrepareEnterGameWorldList();
    if (Core.NeedsCreateWorldOnStartup())
    {
      CurrentStage = Stage::EnterGameCreate;
      sink.Report("prepare", 0.f, "Creating world...");
    }
    else
    {
      CurrentStage = Stage::EnterGameLoad;
      Core.PrepareLoadWorld(Core.GetAppSettings().DefaultWorld);
      PendingWorldName = Core.GetAppSettings().DefaultWorld;
      sink.Report("prepare", 0.f, "Loading world...");
    }
    return false;

  case Stage::EnterGameCreate:
    Core.PrepareStartupWorldCreation();
    Request.settings = Core.GetProceduralTemplate();
    Request.packs = Core.GetDefaultResourcePackSelection();
    CurrentStage = Stage::PrepareCreate;
    PendingWorldOp = WorldRunnerOp::Create;
    return false;

  case Stage::EnterGameLoad:
    PendingWorldOp = WorldRunnerOp::Load;
    CurrentStage = Stage::WorldOperation;
    sink.Begin(WorldOperationKind::Load);
    return false;

  case Stage::ShutdownQuiesce:
    sink.Begin(WorldOperationKind::Shutdown);
    // 100ms/step: active chunk populate often takes 2–4s; 50ms was too short
    // per pass and left workers alive into DrainAsyncIo.
    if (!World.TickBackgroundQuiesce(ShutdownQuiesceState,
                                     std::chrono::milliseconds(100), &sink))
    {
      return false;
    }
    if (Request.shutdownSaveSession)
    {
      CurrentStage = Stage::ShutdownSave;
      return false;
    }
    CurrentStage = Stage::ShutdownFinalize;
    return false;

  case Stage::ShutdownSave:
  {
    const std::string folder = Core.GetActiveWorldFolder().string();
    if (!folder.empty())
    {
      if (!World.HasActiveCooperativeOperation())
      {
        sink.Report("save", 0.92f, "Saving world...");
        // Do not resume streaming after quit-save — PrepareForShutdown follows.
        World.BeginCooperativeSave(folder, /*resume_streaming_after_save=*/false);
      }
      if (!World.TickCooperativeSave(sink, budget))
      {
        return false;
      }
    }
    CurrentStage = Stage::ShutdownFinalize;
    return false;
  }

  case Stage::ShutdownFinalize:
    if (Request.shutdownCloseApplication)
    {
      World.PrepareForShutdown();
    }
    else
    {
      World.ResumeAfterSessionSave();
    }
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  default:
    break;
  }

  return CurrentStage == Stage::Done || CurrentStage == Stage::Failed;
}

} // namespace cutum
