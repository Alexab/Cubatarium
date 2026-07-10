#pragma once

#include "Core/Progress/IUProgressSink.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <functional>
#include <string>

namespace cutum
{

class UCore;
class UWorld;

enum class WorldRunnerOp
{
  Save,
  Load,
  Create,
  SaveThenLoad,
  SaveThenCreate,
  EnterGame
};

struct WorldRunnerRequest
{
  WorldRunnerOp op{WorldRunnerOp::Load};
  std::string worldName;
  ProceduralSettings settings;
  ResourcePackSelection packs;
  bool enterGameAfter{false};
  bool saveConfigAfter{true};
};

class UWorldOperationRunner
{
public:
  UWorldOperationRunner(UCore &core, UWorld &world);

  void Start(WorldRunnerRequest request);
  bool IsActive() const { return Active; }
  /// @return true when the operation has finished (success or failure).
  bool Tick(IUProgressSink &sink, int chunkBudgetPerFrame);
  bool Succeeded() const { return Success; }
  const std::string &ErrorMessage() const { return Error; }
  bool ShouldEnterGame() const { return Request.enterGameAfter; }
  bool IsEnterGameGpuWarmupStage() const
  {
    return CurrentStage == Stage::EnterGameGpuWarmup;
  }
  int EnterGameGpuWarmupFramesRemaining() const
  {
    return EnterGameGpuWarmupFramesLeft;
  }
  /// @return true when GPU warmup stage finished.
  bool AdvanceEnterGameGpuWarmup(IUProgressSink &sink);

private:
  enum class Stage
  {
    Idle,
    PrepareCreate,
    WorldOperation,
    PostLoadUsers,
    PostCreateUsers,
    PostCreateSave,
    EnterGameList,
    EnterGameCreate,
    EnterGameLoad,
    EnterGameFinalize,
    EnterGameGpuWarmup,
    Done,
    Failed
  };

  void Fail(const std::string &message, IUProgressSink &sink);
  bool TickWorldOp(IUProgressSink &sink, int chunkBudget);
  void PrepareCreateWorld();

  UCore &Core;
  UWorld &World;
  WorldRunnerRequest Request;
  Stage CurrentStage{Stage::Idle};
  bool Active{false};
  bool Success{false};
  std::string Error;
  std::string PendingWorldName;
  bool SaveBeforeOp{false};
  WorldRunnerOp PendingWorldOp{WorldRunnerOp::Load};
  int EnterGameGpuWarmupFramesLeft{0};
};

} // namespace cutum
