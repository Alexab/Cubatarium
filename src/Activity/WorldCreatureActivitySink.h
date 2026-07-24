#ifndef WORLDCREATUREACTIVITYSINK_H
#define WORLDCREATUREACTIVITYSINK_H

#include "Activity/IUCreatureActivitySink.h"

namespace cutum
{

class UWorld;

class UWorldCreatureActivitySink : public IUCreatureActivitySink
{
public:
  explicit UWorldCreatureActivitySink(UWorld &world);

  std::optional<CreatureActivityView>
  GetCreatureView(CreatureId Id) const override;
  std::optional<CreatureBehaviorSnapshot>
  GetBehaviorSnapshot(CreatureId Id) const override;
  void SetIntent(CreatureId Id, const CreatureIntent &intent) override;
  const UWorld &GetWorld() const override { return World; }

private:
  UWorld &World;
};

} // namespace cutum

#endif
