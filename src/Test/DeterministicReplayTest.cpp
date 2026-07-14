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
    static const uint64_t kExpectedHashes[] = {
        0x93468bdf1f619622ULL, 0x182554fc9ff246b2ULL, 0xa8e3c1f15e441403ULL,
        0x5a7f9b39cdb01df4ULL, 0x0ecf8f91796d2e91ULL, 0x905359e379fff4beULL,
        0x994f87baa6cd20bfULL, 0x0ee203fbea9756beULL, 0xe2de56ffb96d6ac2ULL,
        0x76b13c2d15cfc83cULL, 0xf60518a53e3efbc1ULL, 0xe401a6c1d9dcc97eULL,
        0x02c8ccd8419cd712ULL,
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
