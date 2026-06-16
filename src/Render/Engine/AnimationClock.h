#ifndef ANIMATIONCLOCK_H
#define ANIMATIONCLOCK_H

namespace cutum
{

/// Global block texture Animation clock (Minecraft-Style 20 ticks per second).
class UAnimationClock
{
public:
  void Tick(float deltaSeconds);
  int CurrentFrame() const { return FrameIndex; }
  int FrameCount() const { return TotalFrames; }
  void SetFrameCount(int count);
  void SetFrametimeTicks(int ticks);

private:
  int FrameIndex{0};
  int TotalFrames{1};
  int FrametimeTicks{2};
  float FrameDuration{0.1f};
  float Accumulator{0.0f};
};

} // namespace cutum

#endif
