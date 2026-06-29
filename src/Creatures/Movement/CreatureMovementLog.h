#ifndef CREATUREMOVEMENTLOG_H
#define CREATUREMOVEMENTLOG_H

#include "Creatures/Movement/CreatureBodyProbe.h"
#include "Creatures/Movement/CreatureFootprint.h"
#include "Creatures/Movement/CreatureHabitatPolicy.h"
#include "Creatures/Movement/CreaturePlacement.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

using CreatureId = uint64_t;

class UWorld;

enum class WanderPickFailure
{
  None,
  HabitatIdle,
  Habitat,
  Collision,
};

const char *WanderPickFailureLabel(WanderPickFailure reason);
const char *HabitatContextLabel(HabitatContext ctx);

void LogCreatureSpawnOk(CreatureId id, const std::string &speciesId,
                        const std::string &layout);
void LogCreatureSpawnFailed(const std::string &speciesId,
                            SpawnFailureReason reason);
void LogCreatureSpawnOverlapRejected(const std::string &speciesId);
void LogCreatureSpawnBlockedAfterPlacement(const std::string &speciesId,
                                           SpawnFailureReason reason);

void LogCreatureWanderNoDirection(CreatureId id, const std::string &speciesId,
                                    const glm::vec3 &bodyOrigin,
                                    bool habitatOk, WanderPickFailure reason);

void LogCreatureMoveProbe(CreatureId id, const UWorld &world,
                          const glm::vec3 &origin, const glm::vec3 &delta,
                          const BodyMoveResult &result, HabitatContext ctx,
                          const EnvironmentSample &originEnv,
                          const FootprintSample &originFootprint);

} // namespace cutum

#endif
