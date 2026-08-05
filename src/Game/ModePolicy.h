#ifndef MODE_POLICY_H
#define MODE_POLICY_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

/// Single gate table for dig / combat / vitals / wear / aggro / economy / UI.
struct ModePolicy
{
  static bool AllowsNeedsTick(WorldGameMode mode)
  {
    return mode == WorldGameMode::Survival;
  }

  static bool AllowsCombatDamage(WorldGameMode mode)
  {
    return mode == WorldGameMode::Survival;
  }

  static bool AllowsDigProgress(WorldGameMode mode, float hardness)
  {
    if (mode == WorldGameMode::Creative)
    {
      return true;
    }
    return hardness > 0.f;
  }

  static bool IsCreativeInstantDig(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool AllowsToolWear(WorldGameMode mode, WorldDifficulty difficulty)
  {
    if (mode == WorldGameMode::Creative)
    {
      return false;
    }
    if (difficulty == WorldDifficulty::Peaceful)
    {
      return false;
    }
    return true;
  }

  static bool AllowsHostileAggro(WorldGameMode mode,
                                 WorldDifficulty difficulty)
  {
    if (mode != WorldGameMode::Survival)
    {
      return false;
    }
    return difficulty != WorldDifficulty::Peaceful;
  }

  static bool AllowsStatusDot(WorldGameMode mode)
  {
    return mode == WorldGameMode::Survival;
  }

  static bool AllowsFlight(WorldGameMode mode, CreatureHabitat habitat)
  {
    if (mode == WorldGameMode::Creative)
    {
      return true;
    }
    return habitat == CreatureHabitat::Aerial;
  }

  static bool AllowsCreativePalette(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool AllowsInstantDelete(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool AllowsFreePlacement(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool ConsumesResourcesOnPlace(WorldGameMode mode)
  {
    return mode == WorldGameMode::Survival;
  }

  static bool DropsBlocksOnBreak(WorldGameMode mode)
  {
    return mode == WorldGameMode::Survival;
  }

  static bool AllowsMobSpawnFromPalette(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool AllowsQaSpawner(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }

  static bool ShouldInitCreativeDefaults(WorldGameMode mode)
  {
    return mode == WorldGameMode::Creative;
  }
};

} // namespace cutum

#endif
