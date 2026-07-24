#include "Activity/Agents/WanderActivityAgent.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace cutum
{
std::filesystem::path GetExecutableDirectory()
{
  return std::filesystem::temp_directory_path();
}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "wander_interval_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  float minInterval = 0.0f;
  float maxInterval = 0.0f;

  cutum::NormalizeWanderIntervalRange(-1.0f, -5.0f, minInterval, maxInterval);
  Expect(minInterval == 2.0f && maxInterval == 4.0f,
         "invalid values should fallback to defaults");

  cutum::NormalizeWanderIntervalRange(6.0f, 3.0f, minInterval, maxInterval);
  Expect(minInterval == 3.0f && maxInterval == 6.0f,
         "range should be ordered ascending");

  cutum::NormalizeWanderIntervalRange(1.5f, 2.5f, minInterval, maxInterval);
  Expect(minInterval == 1.5f && maxInterval == 2.5f,
         "valid range should remain untouched");

  std::cout << "wander_interval_test: OK" << std::endl;
  return 0;
}
