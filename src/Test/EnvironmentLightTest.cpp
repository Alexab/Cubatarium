#include "Render/Engine/EnvironmentLight.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "environment_light_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::SkyLightScaleFromWeather;
  // Clear sky keeps full scale; light rain barely changes it.
  Expect(std::abs(SkyLightScaleFromWeather(1.0f, 0.0f) - 1.0f) < 1e-6f,
         "clear sky is full scale");
  Expect(std::abs(SkyLightScaleFromWeather(1.0f, 0.04f) - 1.0f) < 1e-6f,
         "drizzle below the threshold does not dim");
  // Heavy precipitation dims by at most 12 %.
  Expect(std::abs(SkyLightScaleFromWeather(1.0f, 1.0f) - 0.88f) < 1e-5f,
         "full precipitation dims by 12 percent");
  // Clamped to the 0.82..1.0 range used by the block shaders.
  Expect(SkyLightScaleFromWeather(0.1f, 0.0f) >= 0.82f, "lower clamp");
  Expect(SkyLightScaleFromWeather(3.0f, 0.0f) <= 1.0f, "upper clamp");
  std::cout << "environment_light_test: OK" << std::endl;
  return 0;
}
