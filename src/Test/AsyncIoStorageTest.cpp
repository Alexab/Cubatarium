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
    const auto malformed = cutum::io_codec_fixture::CheckMalformedJson();
    const auto receipt = cutum::io_codec_fixture::CheckReceiptTemporaries();
    const auto streams = cutum::io_codec_fixture::CheckFileStreams(folder + "-streams");
    auto result = cutum::RunAsyncIoStorageProbe(folder);
    result["ordinary_codec"] = codec;
    result["ordinary_disk_index"] = disk;
    result["ordinary_failed_parse"] = malformed;
    result["ordinary_receipt_temporaries"] = receipt;
    result["ordinary_file_streams"] = streams;
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
