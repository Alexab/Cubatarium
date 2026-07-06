#include "Render/Camera/CameraBasisLogic.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

constexpr const char *kTestName = "camera_stability_test";

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

static void TestPitchSweepNoGimbalSnap()
{
  glm::vec3 front;
  glm::vec3 right;
  glm::vec3 up;
  glm::vec3 prevUp(0.0f, 1.0f, 0.0f);
  float prevFrontY = 0.0f;

  for (int pitchDeg = 0; pitchDeg <= 89; ++pitchDeg)
  {
    cutum::ComputeFpsCameraBasis(30.0f, static_cast<float>(pitchDeg), front,
                                 right, up);
    Expect(std::isfinite(up.x) && std::isfinite(up.y) && std::isfinite(up.z),
           "camera basis stays finite while pitching down");
    if (pitchDeg > 0)
    {
      Expect(glm::length(up - prevUp) < 0.35f,
             "camera up vector does not snap while pitching down");
      Expect(front.y + 1e-4f >= prevFrontY,
             "forward pitch increases monotonically toward down");
    }
    prevUp = up;
    prevFrontY = front.y;
  }

  cutum::ComputeFpsCameraBasis(30.0f, 89.0f, front, right, up);
  Expect(front.y > 0.98f, "pitch 89 looks straight down");
}

} // namespace

int main()
{
  TestPitchSweepNoGimbalSnap();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
