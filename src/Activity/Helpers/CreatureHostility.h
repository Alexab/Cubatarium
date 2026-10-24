#ifndef CREATUREHOSTILITY_H
#define CREATUREHOSTILITY_H

#include "Activity/CreatureActivityTypes.h"
#include <string>

namespace cutum
{

class UWorld;

/// True when the species definition marks it hostile (melee behavior or
/// monster tags) — the same rule the companion bot uses to pick threats.
bool IsHostileSpecies(const UWorld &world, const std::string &typeId);

/// Faction rule for the mob↔mob melee fallback (audit M3): hostile mobs hunt
/// prey, not each other. Players, possessed creatures, bots and the mob's own
/// species are never fallback targets.
bool IsMeleeFallbackTarget(const UWorld &world, CreatureId selfId,
                           CreatureId candidateId);

} // namespace cutum

#endif
