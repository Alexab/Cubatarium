#ifndef BARE_HAND_TOOL_INFLUENCE_PROVIDER_H
#define BARE_HAND_TOOL_INFLUENCE_PROVIDER_H

#include "Creatures/Influence/IUToolInfluenceProvider.h"

namespace cutum
{

/// Default provider until the tools agent supplies item-based capabilities.
class UBareHandToolInfluenceProvider : public IUToolInfluenceProvider
{
public:
  bool TryGetCapability(const UCreature &source, InfluenceChannel channel,
                        InfluenceCapability &out) const override;
};

} // namespace cutum

#endif
