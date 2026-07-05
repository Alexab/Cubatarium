#ifndef PHYSICSTELEMETRY_H
#define PHYSICSTELEMETRY_H

#include <cstdint>

namespace cutum
{

struct PhysicsTelemetry
{
  double PhysicsStepMs{0.0};
  double MovementStepMs{0.0};
  double BlockStepMs{0.0};
  double DrainStepMs{0.0};
  double FluidStepMs{0.0};
  int SimulationStepsThisFrame{0};
  uint64_t BlockQueueDepth{0};
  uint64_t LiquidQueueDepth{0};
  uint64_t CollisionRebuildBacklog{0};
  uint64_t VisualRemeshBacklog{0};
  uint64_t DeferredUpdates{0};
  uint64_t DroppedUpdates{0};
  uint64_t PurgedUpdates{0};
  uint64_t CollisionBroadphaseRejects{0};
  uint64_t CollisionBroadphaseFallbacks{0};
  uint64_t CollisionReadyTransitions{0};
  double CollisionReadyWaitMs{0.0};
};

} // namespace cutum

#endif // PHYSICSTELEMETRY_H
