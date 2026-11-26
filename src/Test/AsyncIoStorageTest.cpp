#include "Core/Jobs/JobThreadPool.h"
#include "Test/AsyncIoStorageFixture.h"
#include "Test/IoCodecAllocationFixture.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
nlohmann::json CheckWorkerLaunchAllocations()
{
  using cutum::UJobThreadPool;
  using cutum::io_codec_fixture::Require;
  constexpr size_t workers = 3;
  { UJobThreadPool warmup(workers); }
  const auto launch = UJobThreadPool::GetWorkerLaunchRequestBytes();
  if (!launch)
    return {{"supported", false}};
  size_t vector_bytes = 0;
  observation::Counts started;
  {
    observation::Scope observed;
    {
      UJobThreadPool pool(workers);
      const auto slots = pool.GetWorkerStorageSnapshot();
      vector_bytes = slots.thread_bytes;
      const auto queue = pool.GetQueueAllocationSnapshot();
      started = observation::Snapshot();
      Require(slots.workers == workers && started.live == vector_bytes + queue.live_bytes + workers * launch &&
                  started.blocks == 1 + queue.live_allocations + workers,
              "actual worker launch requests match vector, queue and active entry allocations");
    }
    const auto retired = observation::Snapshot();
    Require(!retired.live && !retired.blocks, "worker vector and cross-thread entries retire");
  }
  for (size_t index = 0; index < started.attempts; ++index)
  {
    observation::Scope refused(index);
    bool failed = false;
    try { UJobThreadPool pool(workers); }
    catch (const std::bad_alloc &) { failed = true; }
    const auto retired = observation::Snapshot();
    Require(failed && retired.refusals == 1 && !retired.live && !retired.blocks,
            "partially launched pool joins before releasing its construction state");
  }
  return {{"supported", true}, {"workers", workers}, {"vector_bytes", vector_bytes},
          {"launch_request_per_worker", launch}, {"ordinary_live", started.live},
          {"refused_prefixes", started.attempts}, {"retired", true}};
}
} // namespace

int main(int argc, char **argv)
{
  try
  {
    const std::filesystem::path report = argc > 1 ? argv[1] : "async-io-storage.json";
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = report.string() + ".data-" + std::to_string(stamp);
    // Codec observations run before any worker starts.
    const auto codec = cutum::io_codec_fixture::CheckCodecs();
    const auto disk = cutum::io_codec_fixture::CheckDiskIndex(folder + "-index");
    const auto malformed = cutum::io_codec_fixture::CheckMalformedJson();
    const auto receipt = cutum::io_codec_fixture::CheckReceiptTemporaries();
    const auto streams = cutum::io_codec_fixture::CheckFileStreams(folder + "-streams");
    const auto launch = CheckWorkerLaunchAllocations();
    auto result = cutum::RunAsyncIoStorageProbe(folder);
    result["ordinary_codec"] = codec;
    result["ordinary_disk_index"] = disk;
    result["ordinary_failed_parse"] = malformed;
    result["ordinary_receipt_temporaries"] = receipt;
    result["ordinary_file_streams"] = streams;
    result["ordinary_worker_launch"] = launch;
    std::ofstream output(report);
    output << result.dump(2) << '\n';
    return output ? 0 : 1;
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
