#ifndef ITEM_VISUAL_DEFAULTS_H
#define ITEM_VISUAL_DEFAULTS_H

#include "Items/ItemDefinition.h"
#include "Items/ItemVisualPresetLibrary.h"

#include <string>

namespace cutum
{

inline bool IdContains(const std::string &id, const char *needle)
{
  return id.find(needle) != std::string::npos;
}

inline bool HasType(const ItemDefinition &def, const char *type)
{
  for (const auto &t : def.Types)
  {
    if (t == type)
    {
      return true;
    }
  }
  return false;
}

/// Category wield scale when Visual.HasWieldScale is false / WieldScale<=0.
inline float DefaultWieldScale(const ItemDefinition &def)
{
  if (def.Visual.HasWieldScale && def.Visual.WieldScale > 0.f)
  {
    return def.Visual.WieldScale;
  }
  if (def.Ranged.Enabled || IdContains(def.Id, "bow"))
  {
    return 1.40f;
  }
  if (def.Block.Enabled || IdContains(def.Id, "shield"))
  {
    return 1.50f;
  }
  if (IdContains(def.Id, "spear"))
  {
    return 1.80f;
  }
  if (HasType(def, "mining") || HasType(def, "cutting") ||
      HasType(def, "digging"))
  {
    return 1.50f;
  }
  if (IdContains(def.Id, "sword") || IdContains(def.Id, "dagger") ||
      IdContains(def.Id, "knife"))
  {
    return 1.35f;
  }
  if (def.Use.Action == ItemUseActionKind::Eat ||
      def.Use.Action == ItemUseActionKind::Drink)
  {
    return 0.95f;
  }
  return 1.25f;
}

inline std::string DefaultSwingPreset(const ItemDefinition &def,
                                      FpSwingKind kind)
{
  switch (kind)
  {
  case FpSwingKind::Place:
    if (!def.Visual.Swing.Place.empty())
    {
      return def.Visual.Swing.Place;
    }
    return "place_block";
  case FpSwingKind::Melee:
    if (!def.Visual.Swing.Melee.empty())
    {
      return def.Visual.Swing.Melee;
    }
    if (IdContains(def.Id, "spear"))
    {
      return "thrust_spear";
    }
    if (HasType(def, "combat") || IdContains(def.Id, "sword") ||
        IdContains(def.Id, "dagger") || IdContains(def.Id, "knife"))
    {
      return "slash_weapon";
    }
    return "dig_tool";
  case FpSwingKind::Dig:
  default:
    if (!def.Visual.Swing.Dig.empty())
    {
      return def.Visual.Swing.Dig;
    }
    return "dig_tool";
  }
}

inline std::string DefaultUsePreset(const ItemDefinition &def,
                                    const char *useKind)
{
  const std::string kind = useKind ? useKind : "";
  if (kind == "eat")
  {
    return def.Visual.Use.Eat.empty() ? "eat_hand" : def.Visual.Use.Eat;
  }
  if (kind == "drink")
  {
    return def.Visual.Use.Drink.empty() ? "eat_hand" : def.Visual.Use.Drink;
  }
  if (kind == "ranged")
  {
    return def.Visual.Use.Ranged.empty() ? "draw_bow" : def.Visual.Use.Ranged;
  }
  if (kind == "block")
  {
    return def.Visual.Use.Block.empty() ? "raise_shield"
                                        : def.Visual.Use.Block;
  }
  return {};
}

} // namespace cutum

#endif
