#include "World/Physics/Replay/ReplayInputLog.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "deterministic_replay_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool CompareRuns(const std::vector<cutum::ReplayTickHash> &lhs,
                        const std::vector<cutum::ReplayTickHash> &rhs)
{
  if (lhs.size() != rhs.size())
  {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i)
  {
    if (lhs[i].Tick != rhs[i].Tick || lhs[i].StateHash != rhs[i].StateHash)
    {
      return false;
    }
  }
  return true;
}

int main()
{
  cutum::UReplayInputLog log_a;
  log_a.LoadGoldenScenario();
  const std::vector<cutum::ReplayTickHash> run_a =
      log_a.Run(nullptr, glm::ivec3(0), glm::ivec3(0));

  cutum::UReplayInputLog log_b;
  log_b.LoadGoldenScenario();
  const std::vector<cutum::ReplayTickHash> run_b =
      log_b.Run(nullptr, glm::ivec3(0), glm::ivec3(0));

  Expect(!run_a.empty(), "golden scenario must produce tick hashes");
  Expect(CompareRuns(run_a, run_b),
         "replay must be deterministic across repeated runs");

  static const uint64_t kExpectedHashes[] = {
      0x2e7ce5c5cd70b4d4ULL, 0xa51f3ae64de36444ULL, 0x15d9afeb8c5536f5ULL,
      0xe745f5231fa13f02ULL, 0xb3f5e18bab7c0c67ULL, 0x2d6937f9abeed648ULL,
      0x2475e9a074dc0249ULL, 0xb3d86de138867448ULL, 0x5fe438e56b7c4834ULL,
      0xcb8b5237c7deeacaULL, 0x4b3f76bfec2fd937ULL, 0x593bc8db0bcdeb88ULL,
      0xbff2a2c2938df5e4ULL,
  };
  Expect(run_a.size() == sizeof(kExpectedHashes) / sizeof(kExpectedHashes[0]),
         "golden hash count mismatch");
  for (size_t i = 0; i < run_a.size(); ++i)
  {
    Expect(run_a[i].StateHash == kExpectedHashes[i], "golden hash mismatch at tick");
  }

  std::cout << "deterministic_replay_test: OK (" << run_a.size()
            << " tick hashes)" << std::endl;
  return 0;
}
