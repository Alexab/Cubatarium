#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Physics/PhysicsChunkDistance.h"
#include <algorithm>
#include <array>

namespace cutum
{

namespace
{

constexpr std::array<glm::ivec3, 6> kNeighborOffsets = {
    glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),  glm::ivec3(0, -1, 0),
    glm::ivec3(0, 1, 0),  glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1)};

void WakeFluidAdjacency(UWorld &world, glm::ivec3 center)
{
  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    world.TryEnqueueFluidAt(center + offset);
  }
}

} // namespace

void UWorldBlockPhysicsService::SetBudgets(const PhysicsBudgets &budgets)
{
  Budgets = budgets;
  BlockQueue.SetBudgets(budgets);
  FluidQueue.SetBudgets(budgets);
}

void UWorldBlockPhysicsService::SetFeatureFlags(
    const PhysicsFeatureFlags &flags)
{
  Flags = flags;
  FallingSystem.ShadowMode = !flags.EnableFalling || flags.FallingShadowMode;
  FluidSystem.ShadowMode = !flags.EnableFluids || flags.LiquidShadowMode;
  MaterialRules.ShadowMode = !flags.EnableMaterialRules;
}

bool UWorldBlockPhysicsService::ShouldCheckFalling(
    const BlockUpdateEvent &event)
{
  switch (event.Type)
  {
  case BlockUpdateEventType::SupportLost:
  case BlockUpdateEventType::NeighborChanged:
    return true;
  case BlockUpdateEventType::BlockChanged:
    return true;
  case BlockUpdateEventType::LiquidCheck:
    return false;
  }
  return false;
}

void UWorldBlockPhysicsService::PublishBlockChanged(glm::ivec3 blockPos,
                                                    glm::ivec3 chunkCoord,
                                                    uint64_t triggerTick,
                                                    uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::BlockChanged;
  ev.Priority = BlockUpdatePriority::Normal;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::PublishNeighborChanged(glm::ivec3 blockPos,
                                                       glm::ivec3 chunkCoord,
                                                       uint64_t triggerTick,
                                                       uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::NeighborChanged;
  ev.Priority = BlockUpdatePriority::High;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::PublishSupportLost(glm::ivec3 blockPos,
                                                   glm::ivec3 chunkCoord,
                                                   uint64_t triggerTick,
                                                   uint64_t localOrder)
{
  if (!Flags.EnableBlockEvents)
  {
    return;
  }
  BlockUpdateEvent ev;
  ev.Type = BlockUpdateEventType::SupportLost;
  ev.Priority = BlockUpdatePriority::Critical;
  ev.BlockPos = blockPos;
  ev.ChunkCoord = chunkCoord;
  ev.TriggerTick = triggerTick;
  ev.LocalOrder = localOrder;
  BlockQueue.Enqueue(ev);
}

void UWorldBlockPhysicsService::ResetRuntimeState()
{
  BlockQueue.Clear();
  FluidQueue.Clear();
  FluidQueue.SetBudgets(Budgets);
  BlockQueue.SetBudgets(Budgets);
}

void UWorldBlockPhysicsService::ClearFluidQueue()
{
  FluidQueue.Clear();
  FluidQueue.SetBudgets(Budgets);
}

void UWorldBlockPhysicsService::PublishFluid(glm::ivec3 blockPos)
{
  if (!Flags.EnableFluids)
  {
    return;
  }
  FluidQueue.Enqueue(blockPos);
}

void UWorldBlockPhysicsService::WakeNearbyFluids(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 center, int radius_blocks)
{
  if (!Flags.EnableFluids)
  {
    return;
  }
  FluidQueue.EnqueueFrontier(blockWorld, definitions, center, radius_blocks);
  PublishFluid(center);
}

void UWorldBlockPhysicsService::ProcessFluidChange(
    UWorld &world, const FluidSpreadChange &change)
{
  world.MarkBlockChunkDirtyFromPhysics(change.BlockPos);
  world.MarkBlockChunkDirtyFromPhysics(change.NeighborPos);

  world.TryEnqueueFluidAt(change.BlockPos);
  world.TryEnqueueFluidAt(change.NeighborPos);
  WakeFluidAdjacency(world, change.BlockPos);
  if (change.NeighborPos != change.BlockPos)
  {
    WakeFluidAdjacency(world, change.NeighborPos);
  }

  if (change.RemovedFluid && world.GetBlockWorld().IsAir(change.BlockPos))
  {
    world.PublishBlockPhysicsEvent(change.BlockPos);
  }

  if (Flags.EnableMaterialRules)
  {
    auto apply_reactions =
        [&](const std::vector<MaterialReactionResult> &reactions)
    {
      for (const MaterialReactionResult &reaction : reactions)
      {
        if (!reaction.Applied)
        {
          continue;
        }
        world.MarkBlockChunkDirtyFromPhysics(reaction.BlockPos);
        world.TryEnqueueFluidAt(reaction.BlockPos);
        WakeFluidAdjacency(world, reaction.BlockPos);
        world.PublishBlockPhysicsEvent(reaction.BlockPos);
      }
    };
    apply_reactions(MaterialRules.EvaluateNeighbors(
        world.GetBlockWorld(), world.GetBlockRegistry(), change.BlockPos));
    apply_reactions(MaterialRules.EvaluateNeighbors(
        world.GetBlockWorld(), world.GetBlockRegistry(), change.NeighborPos));
  }
}

void UWorldBlockPhysicsService::TickBlockPhysics(UWorld &world)
{
  const glm::ivec3 focus_chunk = world.GetMovementDiagnostics().feetChunk;
  BlockQueue.SetFocusChunk(focus_chunk);

  int fallingBudget = std::max(0, Budgets.FallingEventsPerTickMax);
  const std::vector<BlockUpdateEvent> events = BlockQueue.PopBudgeted();
  for (const BlockUpdateEvent &event : events)
  {
    if (!Flags.EnableFalling || fallingBudget <= 0 ||
        !ShouldCheckFalling(event))
    {
      if (Flags.EnableMaterialRules)
      {
        MaterialRules.EvaluateNeighbors(
            world.GetBlockWorld(), world.GetBlockRegistry(), event.BlockPos);
      }
      continue;
    }
    if (ChebyshevChunkDistance(event.ChunkCoord, focus_chunk) >
        Budgets.FallingScanRadiusChunks)
    {
      continue;
    }
    const FallingBlocksStats stats = FallingSystem.Tick(world, event);
    world.AccumulateFallingStats(stats);
    if (stats.Applied > 0)
    {
      --fallingBudget;
      const glm::ivec3 below(event.BlockPos.x, event.BlockPos.y - 1,
                             event.BlockPos.z);
      world.MarkBlockChunkDirtyFromPhysics(event.BlockPos);
      world.MarkBlockChunkDirtyFromPhysics(below);
      world.PublishBlockPhysicsEvent(below);
      world.PublishNeighborPhysicsEvents(below);
    }
    if (Flags.EnableMaterialRules)
    {
      MaterialRules.EvaluateNeighbors(world.GetBlockWorld(),
                                      world.GetBlockRegistry(), event.BlockPos);
    }
  }

  const UBlockDefinitionStorage *definitions =
      world.GetBlockRegistry().GetDefinitions();
  const std::vector<glm::ivec3> fluid_events = FluidQueue.PopBudgeted();
  for (glm::ivec3 pos : fluid_events)
  {
    if (!Flags.EnableFluids || definitions == nullptr)
    {
      continue;
    }
    const FluidSpreadStats stats = FluidSystem.Tick(world, pos);
    world.AccumulateFluidStats(stats);
    for (const FluidSpreadChange &change : stats.Changes)
    {
      ProcessFluidChange(world, change);
    }
  }

  world.UpdatePhysicsQueueStats(BlockQueue.GetStats(), FluidQueue.GetStats());
}

} // namespace cutum
