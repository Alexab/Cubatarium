#include "World/IO/ReadyDataAdmission.h"
#include "Core/Jobs/JobThreadPool.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/IO/AsyncChunkIO.h"
#include <cstdlib>
#include <iostream>

namespace
{
void Expect(bool ok, const char *message)
{
  if (!ok)
  {
    std::cerr << "ready_data_admission_test: " << message << '\n';
    std::exit(1);
  }
}
cutum::AsyncChunkLoadResult Result(int id, int distance)
{
  cutum::AsyncChunkLoadResult value;
  value.coord = {distance, 0, 0};
  value.demandId = id;
  value.payload = {static_cast<uint8_t>(id), 31, 73};
  return value;
}
} // namespace

int main()
{
  using namespace cutum;
  const auto rank = [](const AsyncChunkLoadResult &r)
  { return std::tuple{r.coord.x, r.demandId}; };
  const auto required = [](const AsyncChunkLoadResult &r)
  { return r.coord.x == 3; };
  for (const size_t prefix : {2u, 3u, 4u, 0u, 1u})
  {
    UCompletedJobQueue<AsyncChunkLoadResult> pending;
    for (int id = 0; id < 7; ++id)
      pending.Push(Result(id, 0));
    pending.Push(Result(90, 3));
    std::vector<AsyncChunkLoadResult> selected;
    bool refused = false;
    size_t requests = 0;
    {
      observation::Scope allocation(prefix);
      try { selected = DrainReadyDataForConsumers(pending, 4, rank, required, true); }
      catch (const std::bad_alloc &) { refused = true; }
      requests = observation::Snapshot().attempts;
    }
    if (refused)
    {
      Expect(pending.Size() == 8, "ranking allocation refusal preserves every ready result");
      size_t position = 0;
      pending.InspectStorage([](size_t) {}, [&](const auto &result) {
        Expect(result.demandId == (position == 7 ? 90 : position),
               "ranking allocation refusal preserves FIFO and the reserved field dependency");
        ++position;
      });
      selected = DrainReadyDataForConsumers(pending, 4, rank, required, true);
    }
    Expect(requests == (prefix == 0 ? 1 : 2) && refused == (prefix < 2),
           "ranked drain needs only one rank buffer and one returned vector");
    Expect(pending.Size() == 4 && selected.size() == 4 && selected[0].demandId == 90 &&
               selected[1].demandId == 0 && selected[2].demandId == 1 && selected[3].demandId == 2,
           "the next drain recovers the reserved result and ordinary priority");
  }
  UCompletedJobQueue<AsyncChunkLoadResult> tied;
  for (int id = 0; id < 4; ++id)
    tied.Push(Result(id, 0));
  const auto equal_rank = [](const AsyncChunkLoadResult &) { return 0; };
  const auto second_is_required = [](const AsyncChunkLoadResult &r) { return r.demandId == 1; };
  const auto preferred = DrainReadyDataForConsumers(tied, 3, equal_rank, second_is_required, true);
  Expect(preferred.size() == 3 && preferred[0].demandId == 1 && preferred[1].demandId == 0 &&
             preferred[2].demandId == 2 && tied.Size() == 1,
         "a preferred member already in the selected prefix keeps ordinary FIFO ties");
  UCompletedJobQueue<AsyncChunkLoadResult> ready;
  for (int i = 0; i < 96; ++i)
    ready.Push(Result(i, 0));
  ready.Push(Result(200, 3));
  std::size_t available = 0;
  auto batch =
      DrainReadyDataForConsumers(ready, 4, rank, required, true, &available);
  Expect(available == 97 && batch.size() == 4 && batch[0].demandId == 200,
         "existing field dependency overtakes nearer unrelated ready results");
  Expect(batch[1].demandId == 0 && batch[2].demandId == 1 &&
             batch[3].demandId == 2,
         "only first existing opportunity is reserved; ordinary order stays "
         "intact");
  Expect(batch[0].payload == std::vector<uint8_t>({200, 31, 73}),
         "priority selection preserves actual result payload");
  // Model a real time-budget cut after one applied result. Selected suffixes
  // reenter the production completion queue, never becoming new work.
  for (std::size_t i = 1; i < batch.size(); ++i)
    ready.Push(std::move(batch[i]));
  bool prefer_consumer = false;
  for (int tick = 0; tick < 8; ++tick)
  {
    ready.Push(Result(201 + tick, 3));
    batch =
        DrainReadyDataForConsumers(ready, 4, rank, required, prefer_consumer);
    Expect(batch.size() == 4, "existing count budget remains bounded");
    if (!prefer_consumer)
      Expect(batch[0].coord.x == 0,
             "continuous field demand cannot starve ordinary one-apply frames");
    else
      Expect(
          batch[0].coord.x == 3,
          "field dependency retains progress under continuous nearer arrivals");
    for (std::size_t i = 1; i < batch.size(); ++i)
      ready.Push(std::move(batch[i]));
    prefer_consumer = !prefer_consumer;
  }
  const auto before = ready.Size();
  Expect(DrainReadyDataForConsumers(ready, 0, rank, required, true).empty() &&
             ready.Size() == before,
         "zero count admission cannot consume any ready result");
  batch = DrainReadyDataForConsumers(ready, 1, rank, required, false);
  Expect(batch.size() == 1 && batch[0].coord.x == 0,
         "one-result ordinary lane preserves priority without extra admission");
  std::cout << "ready_data_admission_test: PASS\n";
}
