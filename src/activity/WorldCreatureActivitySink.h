#ifndef WORLDCREATUREACTIVITYSINK_H
#define WORLDCREATUREACTIVITYSINK_H

#include "ICreatureActivitySink.h"

namespace cutum {

class World;

class WorldCreatureActivitySink : public ICreatureActivitySink {
 public:
 explicit WorldCreatureActivitySink(World& world);

 std::optional<CreatureActivityView> GetCreatureView(CreatureId id) const override;
 std::optional<CreatureBehaviorSnapshot> GetBehaviorSnapshot(CreatureId id) const override;
 void SetIntent(CreatureId id, const CreatureIntent& intent) override;

 private:
 World& world_;
};

} // namespace cutum

#endif
