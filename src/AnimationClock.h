#ifndef ANIMATIONCLOCK_H
#define ANIMATIONCLOCK_H

namespace cutum {

/// Global block texture animation clock (Minecraft-style 20 ticks per second).
class AnimationClock {
public:
 void Tick(float deltaSeconds);
 int CurrentFrame() const { return currentFrame_; }
 int FrameCount() const { return frameCount_; }
 void SetFrameCount(int count);
 void SetFrametimeTicks(int ticks);

private:
 int currentFrame_{0};
 int frameCount_{1};
 int frametimeTicks_{2};
 float frameDuration_{0.1f};
 float accumulator_{0.0f};
};

} // namespace cutum

#endif
