#include "App/WorldOperationRunner.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "App/Core.h"
#include "Core/Progress/ProgressTypes.h"
#include "World/Core/World.h"
#include <chrono>
#include <iostream>

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
constexpr int kEnterGameGpuWarmupMaxFrames = 16;

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

bool UWorldOperationRunner::AdvanceEnterGameGpuWarmup(IUProgressSink &sink)
{
  if (CurrentStage != Stage::EnterGameGpuWarmup)
  {
    return true;
  }
  const int frame_index =
      kEnterGameGpuWarmupMaxFrames - EnterGameGpuWarmupFramesLeft;
  const float frac =
      0.94f + 0.05f * (static_cast<float>(frame_index + 1) /
                        static_cast<float>(kEnterGameGpuWarmupMaxFrames));
  sink.Report("prepare_view", frac, "Uploading terrain...");
  --EnterGameGpuWarmupFramesLeft;
  const bool min_frames_done =
      frame_index + 1 >= kEnterGameGpuWarmupMinFrames;
  const bool mesh_ready = !World.NeedsEnterGameMeshWarmup();
  if (!mesh_ready)
  {
    World.SetEnterGameWarmupMissingGreedy(World.CountPostLoadRingNotReady());
  }
  // Do not block EnterGame on live streamer settle — cooperative load already
  // prepared spawn; streaming continues in InGame.
  if (EnterGameGpuWarmupFramesLeft > 0 &&
      (!min_frames_done || !mesh_ready))
  {
    return false;
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
      if (Request.op == WorldRunnerOp::SaveThenLoad)
      {
        Core.PrepareLoadWorld(PendingWorldName);
        PendingWorldOp = WorldRunnerOp::Load;
        sink.Begin(WorldOperationKind::Load);
        sink.Report("load", 0.f, "Loading world...");
      }
      else
      {
        PendingWorldOp = WorldRunnerOp::Create;
        CurrentStage = Stage::PrepareCreate;
      }
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
      CurrentStage = Stage::EnterGameGpuWarmup;
      EnterGameGpuWarmupFramesLeft = kEnterGameGpuWarmupMaxFrames;
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
      CurrentStage = Stage::EnterGameGpuWarmup;
      EnterGameGpuWarmupFramesLeft = kEnterGameGpuWarmupMaxFrames;
      return false;
    }
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  case Stage::EnterGameGpuWarmup:
    return false;

  case Stage::EnterGameFinalize:
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
