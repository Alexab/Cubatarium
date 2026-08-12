#pragma once

#include "Core/Progress/IUProgressSink.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "World/View/WorldViewSettings.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "World/Core/World.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
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
  EnterGame,
  Shutdown
};

struct WorldRunnerRequest
{
  WorldRunnerOp op{WorldRunnerOp::Load};
  std::string worldName;
  ProceduralSettings settings;
  ResourcePackSelection packs;
  WorldViewSettings view;
  WorldGameMode gameMode{WorldGameMode::Creative};
  WorldDifficulty difficulty{WorldDifficulty::Normal};
  bool enterGameAfter{false};
  bool saveConfigAfter{true};
  bool shutdownSaveSession{false};
  bool shutdownCloseApplication{true};
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
  bool ShouldCloseApplication() const
  {
    return Request.op == WorldRunnerOp::Shutdown &&
           Request.shutdownCloseApplication;
  }
  bool IsShutdownOperation() const { return Request.op == WorldRunnerOp::Shutdown; }
  bool IsEnterGameGpuWarmupStage() const
  {
    return CurrentStage == Stage::EnterGameGpuWarmup;
  }
  int EnterGameGpuWarmupFramesRemaining() const
  {
    return EnterGameGpuWarmupFramesLeft;
  }
  /// @return true when GPU warmup stage finished.
  bool AdvanceEnterGameGpuWarmup(IUProgressSink &sink, double frame_ms = 0.0);
  void AccumulateEnterLoadMs(double frame_ms);
  bool IsEnterGameOperation() const
  {
    return Request.op == WorldRunnerOp::EnterGame;
  }
  bool EnterVisualCapReached() const;
  bool IsEnterGameAbortDrainMode() const { return EnterGameAbortDrainMode; }

private:
  enum class Stage
  {
    Idle,
    PrepareCreate,
    PreReplaceTerrain,
    WorldOperation,
    PostLoadUsers,
    PostCreateUsers,
    PostCreateSave,
    EnterGameList,
    EnterGameCreate,
    EnterGameLoad,
    EnterGameFinalize,
    EnterGameGpuWarmup,
    ShutdownQuiesce,
    ShutdownSave,
    ShutdownFinalize,
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
  double EnterGameGpuWarmupElapsedMs{0.0};
  double EnterLoadElapsedMs{0.0};
  /// Era33: Create/SaveThenCreate must not force-cap GpuWarmup while visual debt.
  bool EnterGameColdCreate{false};
  /// Era41: peak FOV lit debt for monotonic progress on the bar.
  int EnterGameFovLitPeakDebt{0};
  /// Era42: one-shot warn when lit drain exceeds hard_wall_ms while holding.
  bool EnterGameLitWarnLogged{false};
  /// Era43: force enter after abort_ms while ingress frozen.
  bool EnterGameForceLitAbort{false};
  /// Era43f: mesh abort wall reached — Era44: continues unified drain.
  bool EnterGameForceMeshAbort{false};
  /// Era44: abort-drain mode (gate stays active until ring ready).
  bool EnterGameAbortDrainMode{false};
  bool EnterGameAbortDrainLogged{false};
  bool EnterGameForceInGameLogged{false};
  /// Era44: peak debt for honest combined progress bar.
  int EnterGameFifoPeak{0};
  int EnterGameGpuPeak{0};
  int EnterGameRingPeak{0};
  float EnterGameDisplayProgress{0.0f};
  UBackgroundQuiesceState ShutdownQuiesceState{};
};

} // namespace cutum
