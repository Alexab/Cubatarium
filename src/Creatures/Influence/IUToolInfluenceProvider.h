#ifndef IUTOOL_INFLUENCE_PROVIDER_H
#define IUTOOL_INFLUENCE_PROVIDER_H

#include "Creatures/Influence/InfluenceCapability.h"

namespace cutum
{

class UCreature;

/// Handshake for the tools agent: resolve wielded-item capability for a creature.
/// Until tools exist, return bare-hand defaults derived from creature attributes.
class IUToolInfluenceProvider
{
public:
  virtual ~IUToolInfluenceProvider() = default;

  /// Fills `out` for the active influence channel. Returns false → caller uses
  /// bare-hand fallback.
  virtual bool TryGetCapability(const UCreature &source,
                                InfluenceChannel channel,
                                InfluenceCapability &out) const = 0;
};

} // namespace cutum

#endif
