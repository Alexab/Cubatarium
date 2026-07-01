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
  CreatureHabitat habitat{CreatureHabitat::Terrestrial};
  glm::vec3 boundsSize{0.6f, 1.8f, 0.6f};
};

class IUCreatureActivitySink
{
public:
  virtual ~IUCreatureActivitySink() = default;
  virtual std::optional<CreatureActivityView>
  GetCreatureView(CreatureId Id) const = 0;
  virtual std::optional<CreatureBehaviorSnapshot>
  GetBehaviorSnapshot(CreatureId Id) const = 0;
  virtual void SetIntent(CreatureId Id, const CreatureIntent &intent) = 0;
};

} // namespace cutum

#endif
