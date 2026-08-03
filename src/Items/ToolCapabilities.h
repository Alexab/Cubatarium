#ifndef TOOL_CAPABILITIES_H
#define TOOL_CAPABILITIES_H

#include "Creatures/Stats/CreatureAttributes.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinition.h"
#include "Blocks/BlockDefinition.h"
#include <string>
#include <unordered_map>

namespace cutum
{

struct InventoryEntryRef;

struct DigParams
{
  bool Effective{false};
  float DurationSec{1.0f};
  float WearDelta{0.f};
  std::string MainGroup;
};

/// Creative: no wear. Survival+Peaceful (easiest): no wear.
bool IsToolWearEnabled(WorldGameMode mode, WorldDifficulty difficulty);

/// Infer dig groups when BlockDefinition.DigGroups is empty (TD until block agent fills dig.groups).
std::unordered_map<std::string, int>
InferDigGroups(const BlockDefinition &block);

DigParams ResolveDigParams(const ItemDefinition *tool_or_null,
                           const BlockDefinition &block,
                           const CreatureAttributes &attrs,
                           WorldGameMode mode);

/// Apply wear to a hotbar item entry. Returns true if slot should be cleared (destroy).
bool ApplyItemWear(InventoryEntryRef &entry, const ItemDefinition &def,
                   float wear_delta, bool wear_enabled);

bool TryRepairItem(InventoryEntryRef &entry, const ItemDefinition &def,
                   const std::string &material_id);

} // namespace cutum

#endif
