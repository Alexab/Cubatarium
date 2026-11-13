#ifndef INFLUENCE_APPLIER_H
#define INFLUENCE_APPLIER_H

#include "Creatures/Influence/InfluencePrediction.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

class UWorld;

struct InfluenceApplyResult
{
  bool AnyTargetRemoved{false};
  bool Applied{false};
};

/// Mutate vitals from a prediction and emit InfluenceEvent.
struct InfluenceApplier
{
  static void ApplyImpact(UWorld &world, uint64_t target, uint64_t thrower,
                          float damage, glm::vec3 hitFrom);
  static InfluenceApplyResult Apply(UWorld &world, InfluencePrediction &pred,
                                    WorldGameMode mode, float dt = 0.f);
};

} // namespace cutum

#endif
