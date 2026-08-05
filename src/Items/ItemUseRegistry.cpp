#include "Items/ItemUseRegistry.h"

namespace cutum
{

ItemUseAction ItemUseRegistry::ParseAction(const std::string &value)
{
  if (value == "eat")
  {
    return ItemUseAction::Eat;
  }
  if (value == "drink")
  {
    return ItemUseAction::Drink;
  }
  if (value == "place_block")
  {
    return ItemUseAction::PlaceBlock;
  }
  return ItemUseAction::None;
}

ItemUseParams ItemUseRegistry::FromDefinition(const ItemDefinition &def)
{
  ItemUseParams params;
  params.Action = def.Use.Action;
  params.SatietyDelta = def.Use.Satiety;
  params.ThirstDelta = def.Use.Thirst;
  params.HealthDelta = def.Use.Health;
  return params;
}

} // namespace cutum
