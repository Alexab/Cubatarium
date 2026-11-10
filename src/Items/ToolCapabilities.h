#ifndef TOOL_CAPABILITIES_H
#define TOOL_CAPABILITIES_H

#include "Blocks/BlockDefinition.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinition.h"
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

struct HitParams
{
  bool DidHit{false};
  bool Missed{false};
  float Damage{0.f};
  float IntervalMul{1.f};
  float WearDelta{0.f};
  std::string CancelReason;
};

/// Shared bare-hand fleshy amount from strength (single formula SoT).
int BareHandFleshyDamage(const CreatureAttributes &attrs);

/// Creative: no wear. Survival+Peaceful (easiest): no wear.
bool IsToolWearEnabled(WorldGameMode mode, WorldDifficulty difficulty);

/// Infer dig groups when BlockDefinition.DigGroups is empty (fallback only;
/// packs carry explicit dig.groups via tools/apply_block_dig_groups.py).
std::unordered_map<std::string, int>
InferDigGroups(const BlockDefinition &block);

DigParams ResolveDigParams(const ItemDefinition *tool_or_null,
                           const BlockDefinition &block,
                           const CreatureAttributes &attrs,
                           WorldGameMode mode);

/// Luanti-style punch: damage_groups × armor_groups + interval mul + accuracy miss.
HitParams ResolveHitParams(const ArmorGroups &armor,
                           const ToolCapabilitiesDef &tool,
                           const CreatureAttributes &attrs,
                           float time_from_last_punch_sec);

/// Apply wear to a hotbar item entry. Returns true if slot should be cleared (destroy).
bool ApplyItemWear(InventoryEntryRef &entry, const ItemDefinition &def,
                   float wear_delta, bool wear_enabled);

bool TryRepairItem(InventoryEntryRef &entry, const ItemDefinition &def,
                   const std::string &material_id);

} // namespace cutum

#endif
