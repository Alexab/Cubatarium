#ifndef ITEM_VISUAL_DEFAULTS_H
#define ITEM_VISUAL_DEFAULTS_H

#include "Items/ItemDefinition.h"
#include "Items/ItemVisualPresetLibrary.h"

#include <cmath>
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

inline bool EulerIsZero(const float euler[3])
{
  return std::fabs(euler[0]) < 1e-3f && std::fabs(euler[1]) < 1e-3f &&
         std::fabs(euler[2]) < 1e-3f;
}

inline bool OffsetIsZero(const float offset[3])
{
  return std::fabs(offset[0]) < 1e-3f && std::fabs(offset[1]) < 1e-3f &&
         std::fabs(offset[2]) < 1e-3f;
}

/// Family grip when content leaves euler/offset at zero.
/// Lean in the FP viewmodel is a separate arm bias; these are tool-local.
inline void DefaultWieldPose(const ItemDefinition &def, float offset[3],
                             float euler[3], std::string &fitAxis)
{
  offset[0] = offset[1] = offset[2] = 0.f;
  euler[0] = euler[1] = euler[2] = 0.f;
  fitAxis = "y";
  const std::string &id = def.Id;
  if (IdContains(id, "pickaxe"))
  {
    euler[0] = -8.f;
    euler[2] = 90.f;
    offset[1] = -0.12f;
  }
  else if (IdContains(id, "axe") && !IdContains(id, "pickaxe"))
  {
    euler[0] = -8.f;
    euler[1] = 20.f;
    euler[2] = 80.f;
    offset[1] = -0.1f;
  }
  else if (IdContains(id, "shovel"))
  {
    euler[0] = 12.f;
    euler[2] = 90.f;
    offset[1] = 0.08f;
  }
  else if (IdContains(id, "hoe"))
  {
    euler[0] = -6.f;
    euler[2] = 90.f;
    offset[1] = -0.08f;
  }
  else if (IdContains(id, "sword") || IdContains(id, "spear"))
  {
    euler[0] = -18.f;
    euler[2] = 8.f;
    offset[1] = -0.05f;
  }
  else if (IdContains(id, "bow"))
  {
    euler[2] = 90.f;
    fitAxis = "z";
  }
  else if (IdContains(id, "arrow"))
  {
    euler[2] = 90.f;
  }
  else if (IdContains(id, "shield"))
  {
    euler[1] = 180.f;
    fitAxis = "z";
  }
  else if (IdContains(id, "torch"))
  {
    euler[0] = -10.f;
    offset[1] = 0.12f;
  }
  else if (def.Use.Action == ItemUseActionKind::Eat ||
           def.Use.Action == ItemUseActionKind::Drink ||
           IdContains(id, "apple") || IdContains(id, "bread") ||
           IdContains(id, "meat") || IdContains(id, "waterskin") ||
           IdContains(id, "leather"))
  {
    euler[0] = -25.f;
    fitAxis = "longest";
  }
  else
  {
    fitAxis = "longest";
  }
}

inline void ResolveWieldPose(const ItemDefinition &def, float offset[3],
                             float euler[3], std::string &fitAxis)
{
  DefaultWieldPose(def, offset, euler, fitAxis);
  if (!OffsetIsZero(def.Visual.WieldOffset))
  {
    offset[0] = def.Visual.WieldOffset[0];
    offset[1] = def.Visual.WieldOffset[1];
    offset[2] = def.Visual.WieldOffset[2];
  }
  if (!EulerIsZero(def.Visual.WieldEulerDeg))
  {
    euler[0] = def.Visual.WieldEulerDeg[0];
    euler[1] = def.Visual.WieldEulerDeg[1];
    euler[2] = def.Visual.WieldEulerDeg[2];
  }
  if (!def.Visual.FitAxis.empty() && def.Visual.FitAxis != "longest")
  {
    fitAxis = def.Visual.FitAxis;
  }
  else if (def.Visual.FitAxis == "longest" &&
           (IdContains(def.Id, "apple") || IdContains(def.Id, "bread") ||
            IdContains(def.Id, "meat") || IdContains(def.Id, "shield")))
  {
    fitAxis = "longest";
  }
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
