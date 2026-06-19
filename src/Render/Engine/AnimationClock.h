#ifndef ANIMATIONCLOCK_H
#define ANIMATIONCLOCK_H

namespace cutum
{

/// Global elapsed time for block texture animation (Minecraft-style 20 ticks/s).
class UAnimationClock
{
public:
  void Tick(float deltaSeconds);
  float ElapsedSeconds() const { return Elapsed; }
  void Reset();

private:
  float Elapsed{0.0f};
};

} // namespace cutum

#endif
