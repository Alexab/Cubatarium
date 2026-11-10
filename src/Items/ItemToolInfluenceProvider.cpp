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
  if (channel != InfluenceChannel::Melee && channel != InfluenceChannel::None &&
      channel != InfluenceChannel::Ranged)
  {
    return false;
  }
  const InventoryEntryRef *active = source.GetInventory().GetActiveEntryRef();
  if (Items && active && !active->empty &&
      active->kind == InventoryEntryKind::Item && !active->broken)
  {
    if (const ItemDefinition *def = Items->Get(active->Id))
    {
      if (channel == InfluenceChannel::Ranged || def->Ranged.Enabled)
      {
        if (!def->Ranged.Enabled || def->Tool.Damage.Empty())
        {
          return false;
        }
        out = InfluenceCapability::DefaultBareHand(
            std::max(1, def->Tool.Damage.FleshyOrMelee()));
        out.Id = def->Id;
        out.Channel = InfluenceChannel::Ranged;
        out.FullIntervalSec = def->Tool.FullPunchInterval;
        out.PunchAttackUses = def->Tool.PunchAttackUses;
        out.RangeBlocks = def->Ranged.RangeBlocks > 0.f
                              ? def->Ranged.RangeBlocks
                              : 16.f;
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
      if (!def->Tool.Damage.Empty())
      {
        out = InfluenceCapability::DefaultBareHand(
            std::max(1, def->Tool.Damage.FleshyOrMelee()));
        out.Id = def->Id;
        out.Channel = InfluenceChannel::Melee;
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
  if (channel == InfluenceChannel::Ranged)
  {
    return false;
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
