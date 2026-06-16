#ifndef WORLDCREATUREACTIVITYSINK_H
#define WORLDCREATUREACTIVITYSINK_H

#include "Activity/ICreatureActivitySink.h"

namespace cutum
{

class UWorld;

class UWorldCreatureActivitySink : public ICreatureActivitySink
{
public:
  explicit UWorldCreatureActivitySink(UWorld &world);

  std::optional<CreatureActivityView>
  GetCreatureView(CreatureId Id) const override;
  std::optional<CreatureBehaviorSnapshot>
  GetBehaviorSnapshot(CreatureId Id) const override;
  void SetIntent(CreatureId Id, const CreatureIntent &intent) override;

private:
  UWorld &World;
};

} // namespace cutum

#endif
