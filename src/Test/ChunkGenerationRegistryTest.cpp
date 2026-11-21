#include "World/Chunks/ChunkGenerationToken.h"
#include <cstdlib>
#include <iostream>
#include <glm/glm.hpp>
#include <thread>
#include <vector>

static void Expect(bool ok)
{
  if (!ok) { std::cerr << "generation registry invariant FAIL\n"; std::exit(1); }
}

int main()
{
  cutum::UChunkGenerationRegistry registry;
  const auto empty_allocator=registry.GetAllocationSnapshot();
  const glm::ivec3 coord(3, 0, -2);

  const uint64_t first = registry.Bump(coord);
  Expect(first >= 1);
  Expect(registry.Current(coord).sequence == first);

  std::vector<std::thread> workers;
  workers.reserve(8);
  for (int i = 0; i < 8; ++i)
  {
    workers.emplace_back(
        [&registry, coord]()
        {
          for (int n = 0; n < 1000; ++n)
          {
            (void)registry.Current(coord);
          }
        });
  }
  for (int i = 0; i < 4; ++i)
  {
    registry.Bump(coord);
  }
  for (std::thread &worker : workers)
  {
    worker.join();
  }

  const uint64_t final_seq = registry.Current(coord).sequence;
  Expect(final_seq == first + 4);
  const glm::ivec3 other(7,0,9);
  const auto other_token=registry.Bump(other);
  Expect(registry.Current(coord).sequence==final_seq);
  Expect(!registry.Retire(coord,first));
  Expect(registry.Retire(coord,final_seq));
  Expect(registry.Current(coord).sequence==0);
  const auto fresh=registry.EnsureCurrent(coord);
  Expect(fresh.sequence>other_token && fresh.sequence!=final_seq);
  Expect(!registry.Retire(coord,final_seq));
  Expect(registry.Current(coord).sequence==fresh.sequence);
  Expect(registry.EnsureCurrent(coord).sequence==fresh.sequence);
  uint64_t last=fresh.sequence;
  for(int n=0;n<10000;++n) {
    const glm::ivec3 key(100+n,0,-100-n);
    const auto token=registry.EnsureCurrent(key);
    Expect(token.sequence>last);
    Expect(registry.Retire(key,token.sequence));
    Expect(registry.Current(key).sequence==0);
    last=token.sequence;
  }
  Expect(registry.LiveTokenCount()==2);
  Expect(registry.Retire(coord,fresh.sequence));
  Expect(registry.Retire(other,other_token));
  Expect(registry.LiveTokenCount()==0);
  Expect(registry.GetAllocationSnapshot().live_bytes==empty_allocator.live_bytes);
  std::cout << "generation_registry_allocator empty=" << empty_allocator.live_bytes
      << " after_retirement=" << registry.GetAllocationSnapshot().live_bytes << '\n';
  const auto successor=registry.EnsureCurrent(coord);
  Expect(successor.sequence>last && !registry.Retire(coord,fresh.sequence));
  std::cout << "generation_registry_retirement cohorts=10000 live="
      << registry.LiveTokenCount() << " stale_retire_preserves_successor=1 PASS\n";
  return 0;
}
