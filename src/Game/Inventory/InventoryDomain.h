#pragma once
#include "Game/WorldGameMode.h"
namespace cutum
{
enum class InventoryDomain
{
  Survival,
  Creative
};
inline InventoryDomain DomainForMode(WorldGameMode mode)
{
  return mode == WorldGameMode::Creative ? InventoryDomain::Creative
                                         : InventoryDomain::Survival;
}
} // namespace cutum
