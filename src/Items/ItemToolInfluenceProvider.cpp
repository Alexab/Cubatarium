#include "Items/ItemToolInfluenceProvider.h"
#include "Creatures/Core/Creature.h"
#include "Items/ItemDefinitionStorage.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

UItemToolInfluenceProvider::UItemToolInfluenceProvider(
    const UItemDefinitionStorage *items)
    : Items(items)
{
}

bool UItemToolInfluenceProvider::TryGetCapability(
    const UCreature &source, InfluenceChannel channel,
    InfluenceCapability &out) const
{
  if (channel != InfluenceChannel::Melee && channel != InfluenceChannel::None)
  {
    return false;
  }
  const InventoryEntryRef *active = source.GetInventory().GetActiveEntryRef();
  if (Items && active && !active->empty &&
      active->kind == InventoryEntryKind::Item && !active->broken)
  {
    if (const ItemDefinition *def = Items->Get(active->Id))
    {
      const float melee = def->Tool.Damage.Melee;
      if (melee > 0.f)
      {
        const int fleshy = std::max(
            1, static_cast<int>(std::lround(melee)));
        out = InfluenceCapability::DefaultBareHand(fleshy);
        out.Id = def->Id;
        out.FullIntervalSec = def->Tool.FullPunchInterval;
        return true;
      }
    }
  }
  const int strength = source.GetAttributes().strength;
  const int fleshy =
      std::max(1, static_cast<int>(std::lround(
                      8.f * (0.5f + static_cast<float>(strength) / 20.f))));
  out = InfluenceCapability::DefaultBareHand(fleshy);
  return true;
}

} // namespace cutum
