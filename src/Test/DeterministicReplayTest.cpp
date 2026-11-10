#include "World/Physics/Replay/ReplayInputLog.h"
#include "World/Physics/Replay/ReplayWorldFixture.h"

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

static std::vector<cutum::ReplayTickHash> RunScenario(bool extended)
{
  cutum::UReplayWorldFixture fixture;
  fixture.GetBlockWorld().SetBlock(glm::ivec3(3, 2, 1), 5);

  cutum::UReplayInputLog log;
  log.LoadGoldenScenario();
  if (extended)
  {
    cutum::ReplayAction tick;
    tick.Type = cutum::ReplayActionType::TickQueues;
    for (int i = 0; i < 100; ++i)
    {
      log.Enqueue(tick);
    }
  }

  return log.Run(&fixture.GetBlockWorld(), glm::ivec3(-16, -16, -16),
                 glm::ivec3(16, 16, 16));
}

int main()
{
  const bool extended = std::getenv("EXTENDED") != nullptr;

  const std::vector<cutum::ReplayTickHash> run_a = RunScenario(extended);
  const std::vector<cutum::ReplayTickHash> run_b = RunScenario(extended);

  Expect(!run_a.empty(), "golden scenario must produce tick hashes");
  Expect(CompareRuns(run_a, run_b),
         "replay must be deterministic across repeated runs");

  if (!extended)
  {
    // Refreshed: the original golden predates UFluidUpdateSet and the fluid
    // cell-state rework, and both feed the replay state hash.
    static const uint64_t kExpectedHashes[] = {
        0x4d05f59846c504e8ULL, 0x2332d21854f1b725ULL, 0x41dc9eecdb60ab5eULL,
        0xcfa10b7639ff99f0ULL, 0x18c9d417fe746ba6ULL, 0x6870c02acc2db6f8ULL,
        0x5128299cc6e2f3f9ULL, 0x2c92298c13f91a95ULL, 0xbf303e7a2c0ed95fULL,
        0xd67559f2fe84a297ULL, 0x29beeeb0a7a30a6bULL, 0x46faad8c3ba59a92ULL,
        0x2b7735bfe283d602ULL,
    };
    Expect(run_a.size() == sizeof(kExpectedHashes) / sizeof(kExpectedHashes[0]),
           "golden hash count mismatch");
    for (size_t i = 0; i < run_a.size(); ++i)
    {
      Expect(run_a[i].StateHash == kExpectedHashes[i],
             "golden hash mismatch at tick");
    }
  }

  std::cout << "deterministic_replay_test: OK (" << run_a.size()
            << " tick hashes)" << std::endl;
  return 0;
}
