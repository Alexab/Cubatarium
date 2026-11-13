#include "Game/Physics/ThrowMath.h"
#include <iostream>
#include <limits>
using namespace cutum;
int main()
{
  int errors = 0;
  auto check = [&](bool ok, const char *message)
  {
    if (!ok)
    {
      ++errors;
      std::cerr << message << '\n';
    }
  };
  PhysicalProfile small{1, glm::vec3(.2f), 1}, heavy{15, glm::vec3(.2f), 1},
      wide{1, glm::vec3(1), 1};
  check(CarryHands(small, 10) == 1, "small cargo occupies one hand");
  check(CarryHands(wide, 10) == 2, "bulky cargo occupies two hands");
  check(CarryHands(heavy, 10) == 2, "compact heavy cargo occupies two hands");
  check(CarryHands({21, glm::vec3(.2f), 1}, 10) == 0, "mass limit");
  check(CarryHands({1, glm::vec3(3, .2f, .2f), 1}, 10) == 0, "dimension limit");
  check(ThrowSpeed(small, 10, 1) > ThrowSpeed(heavy, 10, 1),
        "mass reduces range");
  check(ThrowSpeed(small, 10, 1) > ThrowSpeed(wide, 10, 1),
        "size reduces range");
  check(ThrowSpeed(small, 10, 1) > ThrowSpeed(small, 10, 0),
        "windup increases range");
  check(ThrowSpeed(small, 10, 2) == ThrowSpeed(small, 10, 1), "charge capped");
  check(ImpactDamage(10, 2) == 0 && ImpactDamage(10, 0) == 0,
        "safe contact and rest do no damage");
  check(ImpactDamage(10, 1000) == 50, "damage capped");
  const float nan = std::numeric_limits<float>::quiet_NaN();
  check(CarryHands(small, 10, nan) == 0 && CarryHands(small, 10, 0) == 0,
        "invalid carrier dimensions are refused");
  check(ThrowSpeed(small, 10, nan) == 0,
        "invalid windup cannot create a non-finite velocity");
  check(ImpactDamage(nan, 10) == 0 && ImpactDamage(2, -10) == 0 &&
            ImpactDamage(2, 10, nan) == 0,
        "invalid impact inputs cannot damage vitals");
  auto fast = SweepBody({0, 0, 0}, {100, 0, 0}, glm::vec3(.1f), {5, 0, 0},
                        glm::vec3(.5f));
  check(fast && fast->time < .05f && fast->normal.x == -1,
        "fast cargo cannot tunnel through wall");
  check(!SweepBody({0, 2, 0}, {100, 0, 0}, glm::vec3(.1f), {5, 0, 0},
                   glm::vec3(.5f)),
        "parallel miss");
  check(!SweepBody({0, 0, 0}, {-10, 0, 0}, glm::vec3(.1f), {5, 0, 0},
                   glm::vec3(.5f)),
        "moving away is not impact");
  auto crossing = SweepBody({0, 0, 0}, {10, 0, 0}, glm::vec3(.1f), {5, 5, 0},
                            glm::vec3(.5f), {0, -10, 0});
  check(crossing && crossing->time < .55f,
        "moving target intercepted using relative sweep");
  auto touch = SweepBody({0, .6f, 0}, {0, -.1f, 0}, glm::vec3(.1f), {0, 0, 0},
                         glm::vec3(.5f));
  check(touch && touch->normal.y == 1, "floor contact normal");
  for (int fps : {30, 60, 144})
  {
    glm::vec3 p{0, 0, 0}, v{100, 0, 0};
    bool collided = false;
    for (int f = 0; f < fps && !collided; ++f)
    {
      const auto d = v / float(fps);
      auto hit = SweepBody(p, d, glm::vec3(.1f), {5, 0, 0}, glm::vec3(.5f));
      if (hit)
      {
        p += d * hit->time;
        collided = true;
      }
      else
        p += d;
    }
    check(collided && p.x <= 4.401f, "wall stops cargo at every frame rate");
  }
  return errors ? 1 : 0;
}
