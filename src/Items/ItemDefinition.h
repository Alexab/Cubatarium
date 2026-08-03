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
  float Melee{0.f};
};

struct ToolCapabilitiesDef
{
  float FullPunchInterval{1.0f};
  ToolDamageGroups Damage;
  std::unordered_map<std::string, ToolGroupCap> GroupCaps;
};

struct ItemRepairDef
{
  std::vector<std::string> Materials;
  float Amount{0.25f};
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
  ToolCapabilitiesDef Tool;
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
