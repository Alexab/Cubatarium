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

/// Engagement guard shared by the melee fallback and the companion bot: a
/// creature the given mob may fight — not itself, players, possessed
/// creatures, bots or its own species.
bool IsEngageableMob(const UWorld &world, CreatureId selfId,
                     CreatureId candidateId);

/// Faction rule for the mob↔mob melee fallback (audit M3): hostile mobs hunt
/// prey, not each other. Players, possessed creatures, bots and the mob's own
/// species are never fallback targets.
bool IsMeleeFallbackTarget(const UWorld &world, CreatureId selfId,
                           CreatureId candidateId);

/// The hostile flip of IsMeleeFallbackTarget: an engageable mob of a hostile
/// species — the threats the companion bot attacks.
bool IsHostileEngagementTarget(const UWorld &world, CreatureId selfId,
                               CreatureId candidateId);

} // namespace cutum

#endif
