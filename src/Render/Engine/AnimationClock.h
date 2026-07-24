#ifndef ANIMATIONCLOCK_H
#define ANIMATIONCLOCK_H

namespace cutum
{

/// Global elapsed time for block texture animation (20 ticks/s style).
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
