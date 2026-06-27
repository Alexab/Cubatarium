#include "App/WorldOperationRunner.h"
#include "App/Core.h"
#include "Core/Progress/ProgressTypes.h"
#include "World/Core/World.h"
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
  }
  return WorldOperationKind::Load;
}

constexpr int kChunkBudgetPerFrame = 16;

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
  }
}

void UWorldOperationRunner::Fail(const std::string &message, IProgressSink &sink)
{
  Error = message;
  Success = false;
  Active = false;
  CurrentStage = Stage::Failed;
  sink.End(false, message);
  std::cerr << "World operation failed: " << message << std::endl;
}

bool UWorldOperationRunner::TickWorldOp(IProgressSink &sink, int chunkBudget)
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
      World.BeginCooperativeSave(folder);
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
  Core.ApplyNewWorldCreationRequest(Request.settings, Request.packs);
  PendingWorldName = Core.SetupNewWorldForCreation();
}

bool UWorldOperationRunner::Tick(IProgressSink &sink, int chunkBudgetPerFrame)
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
      CurrentStage = Stage::EnterGameFinalize;
      return false;
    }
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

  case Stage::PostCreateUsers:
    World.GenerateUsers();
    CurrentStage = Stage::PostCreateSave;
    sink.Begin(WorldOperationKind::Save);
    sink.Report("save", 0.f, "Saving new world...");
    PendingWorldOp = WorldRunnerOp::Save;
    return false;

  case Stage::PostCreateSave:
    if (!TickWorldOp(sink, budget))
    {
      return false;
    }
    Core.RefreshWorldListAfterSave();
    if (Request.saveConfigAfter)
    {
      Core.SaveConfigFile();
    }
    if (Request.op == WorldRunnerOp::EnterGame)
    {
      CurrentStage = Stage::EnterGameFinalize;
      return false;
    }
    Success = true;
    Active = false;
    CurrentStage = Stage::Done;
    sink.End(true);
    return true;

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

  default:
    break;
  }

  return CurrentStage == Stage::Done || CurrentStage == Stage::Failed;
}

} // namespace cutum
