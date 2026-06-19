#include "Render/Engine/AnimationClock.h"

namespace cutum
{

void UAnimationClock::Tick(float deltaSeconds)
{
  if (deltaSeconds > 0.0f)
  {
    Elapsed += deltaSeconds;
  }
}

void UAnimationClock::Reset()
{
  Elapsed = 0.0f;
}

} // namespace cutum
