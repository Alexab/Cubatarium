#include "Render/Engine/FluidUnderwaterFogLogic.h"

#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>

namespace
{

constexpr const char *kTestName = "underwater_waterline_test";

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

static void TestWaterlineAboveSurface()
{
  const glm::mat3 inv_view_rot(1.0f);
  const glm::vec3 eye(0.0f, 12.0f, 0.0f);
  const float surface_y = 10.5f;
  const float waterline =
      cutum::ComputeScreenWaterlineNdc(eye, surface_y, inv_view_rot);
  Expect(waterline > 0.0f && waterline < 1.0f,
         "eye above surface yields valid waterline");
}

static void TestWaterlineDisabledWithoutSurface()
{
  const glm::mat3 inv_view_rot(1.0f);
  const glm::vec3 eye(0.0f, 5.0f, 0.0f);
  const float waterline =
      cutum::ComputeScreenWaterlineNdc(eye, 1e9f, inv_view_rot);
  Expect(waterline < -1.5f, "missing surface disables waterline");
}

static void TestPartialSubmergeBand()
{
  Expect(cutum::IsPartialSubmerge(10.2f, 10.4f),
         "eye near surface is partial submerge");
  Expect(!cutum::IsPartialSubmerge(11.0f, 10.4f),
         "eye far above surface is not partial submerge");
  Expect(cutum::IsPartialSubmerge(10.2f, 10.4f),
         "eye slightly below surface is partial submerge");
}

} // namespace

int main()
{
  TestWaterlineAboveSurface();
  TestWaterlineDisabledWithoutSurface();
  TestPartialSubmergeBand();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
