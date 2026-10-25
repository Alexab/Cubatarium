#ifndef CREATUREFACTORY_H
#define CREATUREFACTORY_H

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureSpawnSource.h"
#include <glm/glm.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{

struct CreatureDefinition;

struct CreatureBuildParams
{
  CreatureId Id{0};
  glm::vec3 bodyOrigin{0.f};
  CreatureSpawnSource Source{CreatureSpawnSource::LegacySave};
  std::string SkinId;
  /// Flight allowance after mode policy (false strips canFly and lands a
  /// flying terrestrial). Spawn and load both pass their policy verdict so
  /// restored creatures clamp identically to fresh ones (audit S3).
  bool AllowFlight{true};
  /// Save-restore payload: stats block, inventory, yaw/pitch and
  /// movement_mode are read from it when present.
  const nlohmann::json *SavedState{nullptr};
  /// Spawn lets a ControlledDefault definition build a UPlayer; save-load
  /// always builds a plain mob even for the player species.
  bool AllowPlayerRole{true};
};

/// Single construction path for creatures — spawn, save-load, QA and player
/// creation all build here (audit S3: spawn and load used to assemble the
/// entity by hand in parallel, silently diverging).
class CreatureFactory
{
public:
  static std::unique_ptr<UCreature> Build(const CreatureDefinition &definition,
                                          const CreatureBuildParams &params);
};

} // namespace cutum

#endif
