#ifndef WORLDCREATUREACTIVITYSINK_H
#define WORLDCREATUREACTIVITYSINK_H

#include "ICreatureActivitySink.h"

namespace cutum {

class UWorld;

class WorldCreatureActivitySink : public ICreatureActivitySink {
 public:
 explicit WorldCreatureActivitySink(UWorld& world);

 std::optional<CreatureActivityView> GetCreatureView(CreatureId id) const override;
 std::optional<CreatureBehaviorSnapshot> GetBehaviorSnapshot(CreatureId id) const override;
 void SetIntent(CreatureId id, const CreatureIntent& intent) override;

 private:
 UWorld& world_;
};

} // namespace cutum

#endif
