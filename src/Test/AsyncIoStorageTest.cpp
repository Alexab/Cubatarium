#include "Test/AsyncIoStorageFixture.h"
#include "Test/IoCodecAllocationFixture.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char **argv)
{
  try
  {
    const std::filesystem::path report = argc > 1 ? argv[1] : "async-io-storage.json";
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = report.string() + ".data-" + std::to_string(stamp);
    // No I/O worker exists during the synchronous ordinary-new observations.
    const auto codec = cutum::io_codec_fixture::CheckCodecs();
    const auto disk = cutum::io_codec_fixture::CheckDiskIndex(folder + "-index");
    auto result = cutum::RunAsyncIoStorageProbe(folder);
    result["ordinary_codec"] = codec;
    result["ordinary_disk_index"] = disk;
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
