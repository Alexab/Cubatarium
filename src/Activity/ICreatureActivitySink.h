#ifndef ICREATUREACTIVITYSINK_H
#define ICREATUREACTIVITYSINK_H

#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <optional>

namespace cutum
{

struct CreatureBehaviorSnapshot
{
  CreatureBehaviorParams behavior;
  CreatureLocomotionCapabilities locomotion;
};

class ICreatureActivitySink
{
public:
  virtual ~ICreatureActivitySink() = default;
  virtual std::optional<CreatureActivityView>
  GetCreatureView(CreatureId id) const = 0;
  virtual std::optional<CreatureBehaviorSnapshot>
  GetBehaviorSnapshot(CreatureId id) const = 0;
  virtual void SetIntent(CreatureId id, const CreatureIntent &intent) = 0;
};

} // namespace cutum

#endif
