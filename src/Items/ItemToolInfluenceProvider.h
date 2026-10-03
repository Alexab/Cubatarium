#ifndef ITEM_TOOL_INFLUENCE_PROVIDER_H
#define ITEM_TOOL_INFLUENCE_PROVIDER_H

#include "Creatures/Influence/IUToolInfluenceProvider.h"

namespace cutum
{

class UItemDefinitionStorage;

/// Resolves melee capability from the active hotbar Item (fallback: bare hand).
class UItemToolInfluenceProvider : public IUToolInfluenceProvider
{
public:
  explicit UItemToolInfluenceProvider(const UItemDefinitionStorage *items);

  bool TryGetCapability(const UCreature &source, InfluenceChannel channel,
                        InfluenceCapability &out) const override;

private:
  const UItemDefinitionStorage *Items{nullptr};
};

} // namespace cutum

#endif
