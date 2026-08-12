#include "App/WorldOperationRunner.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "App/Core.h"
#include "Core/Progress/ProgressTypes.h"
#include "World/Core/World.h"
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
  return !World.NeedsEnterGameMeshWarmup();
}

bool UWorldOperationRunner::AdvanceEnterGameGpuWarmup(IUProgressSink &sink,
                                                      double frame_ms)
{
  if (CurrentStage != Stage::EnterGameGpuWarmup)
  {
    return true;
  }
  EnterGameGpuWarmupElapsedMs += frame_ms;
  const int fov_debt = World.CountEnterFovLitDebt();
  if (fov_debt > EnterGameFovLitPeakDebt)
  {
    EnterGameFovLitPeakDebt = fov_debt;
  }
  const bool mesh_ready = !World.NeedsEnterGameMeshWarmup();
  const bool fov_ready = fov_debt <= 0;
  const bool soft_ready = mesh_ready && fov_ready;
  const auto &tune = URuntimeTuning::Get();
  // Era42: wait for full enter lit debt==0; hard-wall aborts only if
  // enter_lit_require_zero=false.
  const bool cap_reached = ShouldForceEnterVisualCap(
      EnterGameGpuWarmupElapsedMs, soft_ready, EnterGameColdCreate,
      tune.EnterFovLitHardWallMs, tune.EnterLitRequireZero);
  if (!cap_reached && fov_debt > 0 &&
      EnterGameGpuWarmupElapsedMs >=
          static_cast<double>(tune.EnterFovLitHardWallMs) &&
      !EnterGameLitWarnLogged)
  {
    EnterGameLitWarnLogged = true;
    std::cerr << "[Era42] enter lit still draining past warn wall ("
              << EnterGameGpuWarmupElapsedMs << "ms, lit=" << fov_debt
              << ")\n";
  }
  if (!EnterGameForceLitAbort && tune.EnterLitRequireZero && fov_debt > 0 &&
      tune.EnterLitAbortMs > 0 &&
      EnterGameGpuWarmupElapsedMs >= static_cast<double>(tune.EnterLitAbortMs) &&
      World.IsEnterLitGateActive())
  {
    EnterGameForceLitAbort = true;
    std::cerr << "[Era43] enter lit abort after " << EnterGameGpuWarmupElapsedMs
              << "ms, residual=" << fov_debt << "\n";
    World.EndEnterLitGate();
    UEnterLitDiagnostics::EndSession();
  }
  const float lit_prog =
      EnterFovLitProgressFraction(fov_debt, EnterGameFovLitPeakDebt);
  const float frac = 0.94f + 0.05f * lit_prog;
  std::string status;
  if (fov_debt > 0)
  {
    status = "Lighting… " + std::to_string(fov_debt) + " left";
    if (EnterGameGpuWarmupElapsedMs >=
        static_cast<double>(tune.EnterFovLitHardWallMs))
    {
      status += " (slow)";
    }
  }
  else if (!mesh_ready)
  {
    status = "Uploading terrain...";
  }
  else
  {
    status = "Preparing view...";
  }
  sink.Report("prepare_view", frac, status);
  if (EnterGameGpuWarmupFramesLeft > 0)
  {
    --EnterGameGpuWarmupFramesLeft;
  }
  const int frame_index =
      kEnterGameGpuWarmupMaxFrames - EnterGameGpuWarmupFramesLeft;
  EnterLitSample lit_sample{};
  UEnterLitDiagnostics::Sample(World, EnterGameGpuWarmupElapsedMs, lit_sample);
  UEnterLitDiagnostics::MaybeLog(lit_sample, frame_index);
  const bool min_frames_done =
      frame_index >= kEnterGameGpuWarmupMinFrames;
  if (!mesh_ready)
  {
    World.SetEnterGameWarmupMissingGreedy(World.CountPostLoadRingNotReady());
  }
  // Stay on bar until min frames + FOV/mesh ready, unless hard-wall.
  if ((!min_frames_done || !soft_ready) && !cap_reached && !EnterGameForceLitAbort)
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
