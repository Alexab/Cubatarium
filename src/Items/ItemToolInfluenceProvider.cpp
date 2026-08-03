#include "Items/ItemToolInfluenceProvider.h"
#include "Creatures/Core/Creature.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
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
      if (!def->Tool.Damage.Empty())
      {
        out = InfluenceCapability::DefaultBareHand(
            std::max(1, def->Tool.Damage.FleshyOrMelee()));
        out.Id = def->Id;
        out.FullIntervalSec = def->Tool.FullPunchInterval;
        out.PunchAttackUses = def->Tool.PunchAttackUses;
        out.Damage.Ratings.clear();
        if (!def->Tool.Damage.Groups.empty())
        {
          out.Damage.Ratings = def->Tool.Damage.Groups;
        }
        else
        {
          out.Damage =
              DamageGroups::MeleeFleshy(def->Tool.Damage.FleshyOrMelee());
        }
        return true;
      }
    }
  }
  out = InfluenceCapability::DefaultBareHand(
      source.GetBareHandFleshyOverride() > 0
          ? source.GetBareHandFleshyOverride()
          : BareHandFleshyDamage(source.GetAttributes()));
  if (source.GetBareHandIntervalOverride() > 0.f)
  {
    out.FullIntervalSec = source.GetBareHandIntervalOverride();
  }
  return true;
}

} // namespace cutum
