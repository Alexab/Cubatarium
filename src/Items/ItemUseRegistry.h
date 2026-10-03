#ifndef ITEM_USE_REGISTRY_H
#define ITEM_USE_REGISTRY_H

#include "Items/ItemDefinition.h"

#include <string>

namespace cutum
{

using ItemUseAction = ItemUseActionKind;

struct ItemUseParams
{
  ItemUseAction Action{ItemUseAction::None};
  float SatietyDelta{0.f};
  float ThirstDelta{0.f};
  float HealthDelta{0.f};
};

struct ItemUseRegistry
{
  static ItemUseAction ParseAction(const std::string &value);
  static ItemUseParams FromDefinition(const ItemDefinition &def);
};

} // namespace cutum

#endif
