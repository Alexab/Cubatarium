#include "Creatures/Locomotion/RiderCarry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "rider_carry_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Near(float a, float b, float eps = 1e-4f)
{
  return std::abs(a - b) < eps;
}

int main()
{
  using namespace cutum;

  // Wrap: shortest signed turn.
  Expect(Near(WrapYawDeltaDeg(190.0f), -170.0f), "wrap 190 -> -170");
  Expect(Near(WrapYawDeltaDeg(-190.0f), 170.0f), "wrap -190 -> 170");
  Expect(Near(WrapYawDeltaDeg(45.0f), 45.0f), "wrap 45 unchanged");
  Expect(Near(WrapYawDeltaDeg(180.0f), 180.0f), "wrap 180 stays");

  // Rider standing at the mount's forward tip keeps pointing forward as the
  // mount turns: forward(yaw) = (sin, cos). Mount at origin, yaw 0 -> +Z.
  const glm::vec3 pivot(10.0f, 5.0f, -3.0f);
  const glm::vec3 tip = pivot + glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 turned = RotateRiderAroundMount(tip, pivot, 90.0f);
  // yaw 90 -> forward = (sin 90, cos 90) = +X.
  Expect(Near(turned.x, pivot.x + 1.0f) && Near(turned.z, pivot.z),
         "forward tip follows a 90 degree turn to +X");
  Expect(Near(turned.y, tip.y), "rotation keeps Y");

  // Rider at the pivot does not move.
  const glm::vec3 same = RotateRiderAroundMount(pivot, pivot, 123.0f);
  Expect(Near(same.x, pivot.x) && Near(same.z, pivot.z),
         "rider at pivot is unaffected");

  // Offset length is preserved for any angle.
  const glm::vec3 side = pivot + glm::vec3(0.4f, 0.0f, 0.3f);
  const glm::vec3 r = RotateRiderAroundMount(side, pivot, 37.0f);
  Expect(Near(std::hypot(r.x - pivot.x, r.z - pivot.z), 0.5f),
         "offset length preserved");

  // Full circle returns to the start.
  glm::vec3 p = side;
  for (int i = 0; i < 12; ++i)
  {
    p = RotateRiderAroundMount(p, pivot, 30.0f);
  }
  Expect(Near(p.x, side.x) && Near(p.z, side.z), "12 x 30 degrees = identity");

  // Camera yaw moves the opposite way (forward = (cos, sin)).
  Expect(Near(CameraYawDeltaForMountYaw(25.0f), -25.0f), "camera yaw sign");

  std::cout << "rider_carry_test: ok" << std::endl;
  return 0;
}
