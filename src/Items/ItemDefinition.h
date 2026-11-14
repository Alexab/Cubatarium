#ifndef ITEM_DEFINITION_H
#define ITEM_DEFINITION_H

#include "Game/Physics/ThrowMath.h"
#include "Game/Physics/ThrowSettings.h"
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
  /// Slot ids: {"head","chest","legs","feet"}.
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
  std::string Interaction;
  float Duration{.8f};
  ItemUseActionKind Action{ItemUseActionKind::None};
  /// World block placed by the place_block action; empty = use the item id.
  std::string PlaceBlockId;
  float Satiety{0.f};
  float Thirst{0.f};
  float Health{0.f};
};

struct ItemVisualSwingMap
{
  std::string Dig;
  std::string Melee;
  std::string Place;
};

struct ItemVisualUseMap
{
  std::string Eat;
  std::string Drink;
  std::string Ranged;
  std::string Block;
};

struct ItemVisualDef
{
  float IconScale{1}, FpScale{1}, TpScale{1}, GroundScale{1};
  /// Multiplier after AABB fit; 0 = use category default at resolve time.
  float WieldScale{0.f};
  float WieldOffset[3]{0.f, 0.f, 0.f};
  float WieldEulerDeg[3]{0.f, 0.f, 0.f};
  std::string FitAxis{"longest"};
  bool HasWieldScale{false};
  ItemVisualSwingMap Swing;
  ItemVisualUseMap Use;
};

struct ItemRangedDef
{
  bool Enabled{false};
  float RangeBlocks{16.f};
  std::string AmmoId;
  bool RequireLos{true};
};

struct ItemBlockDef
{
  bool Enabled{false};
  float DamageMul{0.25f};
  float AngleDeg{120.f};
  float PassiveDamageMul{0.85f};
  int BlockUses{0};
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
  ThrowCategory ThrowClass{ThrowCategory::General};
  PhysicalProfile Physical;
  ItemVisualDef Visual;
  ItemRangedDef Ranged;
  ItemBlockDef Block;
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
