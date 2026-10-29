#include "Creatures/Core/UCreatureFactory.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include <nlohmann/json.hpp>

namespace cutum
{

std::unique_ptr<UCreature>
UCreatureFactory::Build(const CreatureDefinition &definition,
                       const CreatureBuildParams &params)
{
  const glm::vec3 eyeOffset(0.0f, definition.eyeHeight, 0.0f);
  std::unique_ptr<UCreature> creature;
  if (definition.role == CreatureRole::ControlledDefault && params.AllowPlayerRole)
  {
    creature = std::make_unique<UPlayer>(params.Id, definition.Id,
                                         params.bodyOrigin, eyeOffset);
  }
  else
  {
    creature = std::make_unique<UCreature>(params.Id, definition.Id,
                                           params.bodyOrigin, eyeOffset);
  }

  creature->GetBoundsMutable().profile = definition.bounds;
  if (definition.bounds.restSizeBlocks.x > 0.0f)
  {
    creature->GetBoundsMutable().currentSizeBlocks = definition.bounds.restSizeBlocks;
  }
  creature->SetCapabilities(definition.locomotion);
  creature->GetLocomotion().SetBodySupportMode(true);
  creature->SetSpawnSource(params.Source);
  creature->ApplyStatsFromDefinition(definition);
  if (params.SavedState &&
      CreatureStatsJson::Read(*params.SavedState, creature->GetVitals(),
                              creature->GetAttributes()))
  {
    creature->GetAttributes().ClampAll();
    creature->GetVitals().ClampCurrents();
  }
  creature->SetLocomotionArchetype(definition.locomotionArchetype);
  creature->SetModelYawOffsetDeg(definition.visual.modelYawOffsetDeg);
  creature->SetWalkCycleHz(definition.visual.Animation.walkCycleHz);
  creature->GetLocomotion().SetCollisionProfile(
      creature->GetBounds().currentSizeBlocks, definition.eyeHeight);
  if (!params.SkinId.empty())
  {
    creature->SetSkinId(params.SkinId);
  }
  creature->SetVisual(CreateCreatureVisual(definition));
  if (params.SavedState)
  {
    creature->SetOrientation(params.SavedState->value("yaw", 0.0f),
                             params.SavedState->value("pitch", 0.0f));
  }
  const bool savedFlying =
      params.SavedState &&
      params.SavedState->value("movement_mode", "walking") == "flying";
  if (definition.habitat != CreatureHabitat::Terrestrial || savedFlying)
  {
    creature->GetLocomotion().SetMode(CreatureMovementMode::Flying);
  }
  if (params.SavedState)
  {
    creature->GetInventory().DeserializeFromJson(*params.SavedState);
  }
  if (!params.AllowFlight)
  {
    // Survival: strip creative fly unless aerial habitat allows it.
    CreatureLocomotionCapabilities caps = definition.locomotion;
    caps.canFly = false;
    creature->SetCapabilities(caps);
    if (definition.habitat == CreatureHabitat::Terrestrial &&
        creature->GetMovementMode() == CreatureMovementMode::Flying)
    {
      creature->GetLocomotion().SetMode(CreatureMovementMode::Walking);
    }
  }
  return creature;
}

} // namespace cutum
