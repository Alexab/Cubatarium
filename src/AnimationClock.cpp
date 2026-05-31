#include "AnimationClock.h"
#include <algorithm>

namespace cutum {

void AnimationClock::SetFrameCount(int count)
{
 frameCount_ = std::max(1, count);
 currentFrame_ %= frameCount_;
}

void AnimationClock::SetFrametimeTicks(int ticks)
{
 frametimeTicks_ = std::max(1, ticks);
 frameDuration_ = static_cast<float>(frametimeTicks_) / 20.0f;
}

void AnimationClock::Tick(float deltaSeconds)
{
 if (deltaSeconds <= 0.0f) {
  return;
 }
 if (frameDuration_ <= 0.0f) {
  frameDuration_ = 2.0f / 20.0f;
 }
 accumulator_ += deltaSeconds;
 while (accumulator_ >= frameDuration_) {
  accumulator_ -= frameDuration_;
  ++currentFrame_;
  if (currentFrame_ > 1000000) {
   currentFrame_ = 0;
  }
 }
}

} // namespace cutum
