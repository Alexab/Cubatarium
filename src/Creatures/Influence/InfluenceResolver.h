#ifndef INFLUENCE_RESOLVER_H
#define INFLUENCE_RESOLVER_H

#include "Creatures/Influence/IUToolInfluenceProvider.h"
#include "Creatures/Influence/InfluencePrediction.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

class UWorld;
class UCreature;

/// Precompute influence results (no mutation). Cancelable before Apply.
struct InfluenceResolver
{
  static InfluencePrediction Resolve(UWorld &world, UCreature &source,
                                     WorldGameMode mode,
                                     const IUToolInfluenceProvider *tools);
};

} // namespace cutum

#endif
