#include "Render/Engine/AnimationClock.h"
#include <algorithm>

namespace cutum
{

void UAnimationClock::SetFrameCount(int count)
{
  TotalFrames = std::max(1, count);
  FrameIndex %= TotalFrames;
}

void UAnimationClock::SetFrametimeTicks(int ticks)
{
  FrametimeTicks = std::max(1, ticks);
  FrameDuration = static_cast<float>(FrametimeTicks) / 20.0f;
}

void UAnimationClock::Tick(float deltaSeconds)
{
  if (deltaSeconds <= 0.0f)
  {
    return;
  }
  if (FrameDuration <= 0.0f)
  {
    FrameDuration = 2.0f / 20.0f;
  }
  Accumulator += deltaSeconds;
  while (Accumulator >= FrameDuration)
  {
    Accumulator -= FrameDuration;
    ++FrameIndex;
    if (FrameIndex > 1000000)
    {
      FrameIndex = 0;
    }
  }
}

} // namespace cutum
