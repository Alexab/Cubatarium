#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cutum
{

namespace
{

constexpr float kSatietyDrainPerSec = 0.15f;
constexpr float kThirstDrainPerSec = 0.2f;
constexpr float kStarveDamagePerSec = 2.0f;
constexpr float kFatigueRecoverPerSec = 4.0f;
constexpr float kFatigueSprintPerSec = 8.0f;
constexpr float kFatigueSwimPerSec = 5.0f;
constexpr float kBreathDrainPerSec = 6.0f;
constexpr float kBreathRecoverPerSec = 20.0f;

struct DifficultyVitalsScale
{
  float NeedsDrain{1.f};
  float FatigueGain{1.f};
  float BreathDrain{1.f};
  bool DrownDamage{true};
};

DifficultyVitalsScale ScaleForDifficulty(WorldDifficulty difficulty)
{
  switch (difficulty)
  {
  case WorldDifficulty::Peaceful:
    return {0.f, 0.f, 0.f, false};
  case WorldDifficulty::Easy:
    return {0.5f, 0.5f, 0.25f, true};
  case WorldDifficulty::Normal:
  default:
    return {1.f, 1.f, 1.f, true};
  }
}

} // namespace

void CreatureVitalsSystem::Tick(UWorld &world, WorldGameMode mode,
                                WorldDifficulty difficulty, float dt)
{
  if (mode != WorldGameMode::Survival || dt <= 0.f)
  {
    return;
  }

  const DifficultyVitalsScale scale = ScaleForDifficulty(difficulty);

  std::vector<CreatureId> toRemove;
  world.ForEachCreature(
      [&](UCreature &creature)
      {
        if (!creature.NeedsNeedsTick())
        {
          return;
        }
        CreatureVitals &v = creature.GetVitals();
        const CreatureAttributes &a = creature.GetAttributes();
        const float endMul =
            1.f / std::max(0.5f, 0.5f + static_cast<float>(a.endurance) / 20.f);

        v.satiety =
            std::max(0.f, v.satiety - kSatietyDrainPerSec * scale.NeedsDrain * dt);
        v.thirst =
            std::max(0.f, v.thirst - kThirstDrainPerSec * scale.NeedsDrain * dt);

        const bool sprinting =
            creature.GetLocomotionFacts().state == LocomotionState::Run;
        const EnvironmentSample env = ProbeEnvironmentAt(
            world, creature.GetBodyOrigin(),
            creature.GetBounds().profile.restSizeBlocks);
        float fatigueGain = 0.f;
        if (sprinting)
        {
          fatigueGain += kFatigueSprintPerSec;
        }
        if (env.inWater)
        {
          fatigueGain += kFatigueSwimPerSec;
          if (scale.BreathDrain > 0.f)
          {
            v.breath = std::max(
                0.f, v.breath - kBreathDrainPerSec * scale.BreathDrain * dt);
            if (v.breath <= 0.f && scale.DrownDamage)
            {
              if (ApplyDamage(world, creature, kStarveDamagePerSec * dt, mode))
              {
                toRemove.push_back(creature.GetId());
              }
            }
          }
          else
          {
            v.breath = v.maxBreath;
          }
        }
        else
        {
          v.breath = std::min(v.maxBreath, v.breath + kBreathRecoverPerSec * dt);
        }

        if (fatigueGain > 0.f && scale.FatigueGain > 0.f)
        {
          v.fatigue = std::min(
              v.maxFatigue,
              v.fatigue + fatigueGain * scale.FatigueGain * endMul * dt);
        }
        else if (fatigueGain <= 0.f)
        {
          v.fatigue = std::max(0.f, v.fatigue - kFatigueRecoverPerSec * dt);
        }

        if (scale.NeedsDrain > 0.f &&
            (v.satiety <= 0.f || v.thirst <= 0.f))
        {
          if (ApplyDamage(world, creature, kStarveDamagePerSec * dt, mode))
          {
            toRemove.push_back(creature.GetId());
          }
        }
        v.ClampCurrents();
      });

  for (CreatureId id : toRemove)
  {
    world.RemoveCreature(id);
  }
}

bool CreatureVitalsSystem::ApplyDamage(UWorld &world, UCreature &target,
                                       float amount, WorldGameMode mode,
                                       const char * /*reason*/)
{
  if (mode == WorldGameMode::Creative || amount <= 0.f)
  {
    return false;
  }
  CreatureVitals &v = target.GetVitals();
  // Flat armor remains as fleshy mitigation; group math is applied upstream
  // in InfluenceHitMath when the hit comes from Influence.
  const float mitigated =
      std::max(0.f, amount - v.armor * 0.5f);
  // Luck: small chance to shrug off a hit.
  if (target.GetAttributes().luck >= 18 && mitigated > 0.f)
  {
    const int roll = static_cast<int>(std::floor(mitigated * 100.f)) % 100;
    if (roll < target.GetAttributes().luck)
    {
      return false;
    }
  }
  v.health = std::max(0.f, v.health - mitigated);
  if (v.health <= 0.f)
  {
    return HandleLethal(world, target, mode);
  }
  return false;
}

bool CreatureVitalsSystem::HandleLethal(UWorld &world, UCreature &target,
                                        WorldGameMode mode)
{
  if (mode == WorldGameMode::Creative)
  {
    target.GetVitals().health = target.GetVitals().maxHealth;
    return false;
  }
  CreatureVitals &v = target.GetVitals();
  v.fatalWounds = std::min(v.maxFatalWounds, v.fatalWounds + 1);
  if (v.fatalWounds >= v.maxFatalWounds)
  {
    if (target.IsPlayerCharacter())
    {
      // Permanent death: keep entity, spectate via free-move fly, low HP.
      v.FillFull();
      v.fatalWounds = v.maxFatalWounds;
      v.health = 1.f;
      v.satiety = 0.f;
      v.thirst = 0.f;
      if (auto camera = world.GetCurrentUserCamera())
      {
        camera->SetFreeMove(true);
        target.GetLocomotion().SetMode(CreatureMovementMode::Flying);
      }
      return false;
    }
    return true;
  }
  v.FillFull();
  v.health = v.maxHealth;
  const glm::vec3 spawn = world.GetSpawnPoint();
  target.SetBodyOrigin(glm::vec3(spawn.x, spawn.y - target.GetEyeOffset().y,
                                 spawn.z));
  return false;
}

} // namespace cutum
