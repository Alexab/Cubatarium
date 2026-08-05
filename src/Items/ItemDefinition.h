#ifndef ITEM_DEFINITION_H
#define ITEM_DEFINITION_H

#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

enum class ItemWearEnd
{
  Destroy,
  Broken,
  Indestructible,
};

struct ToolGroupCap
{
  int MaxLevel{1};
  int Uses{20};
  /// Rating (1=hardest .. 3=softest) → dig time seconds.
  std::unordered_map<int, float> Times;
};

struct ToolDamageGroups
{
  /// Legacy scalar; if Groups empty and Melee > 0, treated as fleshy.
  float Melee{0.f};
  /// Luanti-style damage_groups (e.g. fleshy → amount).
  std::unordered_map<std::string, int> Groups;

  int FleshyOrMelee() const
  {
    const auto it = Groups.find("fleshy");
    if (it != Groups.end())
    {
      return it->second;
    }
    if (Melee > 0.f)
    {
      return static_cast<int>(Melee);
    }
    return 0;
  }

  bool Empty() const { return Groups.empty() && !(Melee > 0.f); }
};

struct ToolCapabilitiesDef
{
  float FullPunchInterval{1.0f};
  int PunchAttackUses{0};
  ToolDamageGroups Damage;
  std::unordered_map<std::string, ToolGroupCap> GroupCaps;
};

struct ItemRepairDef
{
  std::vector<std::string> Materials;
  float Amount{0.25f};
};

struct ItemArmorDef
{
  /// Slot ids: {"head","chest","arms","hands","legs","feet"}.
  std::vector<std::string> Slots;
  /// Luanti-style armor_groups: group -> rating (int).
  std::unordered_map<std::string, int> ArmorGroups;
};

enum class ItemUseActionKind
{
  None = 0,
  Eat,
  Drink,
  PlaceBlock
};

struct ItemUseDef
{
  ItemUseActionKind Action{ItemUseActionKind::None};
  float Satiety{0.f};
  float Thirst{0.f};
  float Health{0.f};
};

struct ItemDefinition
{
  std::string Id;
  std::string DisplayName;
  std::vector<std::string> Types;
  int StackMax{1};
  ItemWearEnd WearEnd{ItemWearEnd::Destroy};
  ItemRepairDef Repair;
  std::string ModelPath;
  ItemArmorDef Armor;
  ToolCapabilitiesDef Tool;
  ItemUseDef Use;
  bool HandFallback{false};
  bool Hidden{false};
};

inline const char *ItemWearEndToString(ItemWearEnd value)
{
  switch (value)
  {
  case ItemWearEnd::Broken:
    return "broken";
  case ItemWearEnd::Indestructible:
    return "indestructible";
  case ItemWearEnd::Destroy:
  default:
    return "destroy";
  }
}

inline ItemWearEnd ItemWearEndFromString(const std::string &value)
{
  if (value == "broken")
  {
    return ItemWearEnd::Broken;
  }
  if (value == "indestructible")
  {
    return ItemWearEnd::Indestructible;
  }
  return ItemWearEnd::Destroy;
}

} // namespace cutum

#endif
