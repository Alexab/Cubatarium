#include "World/Chunks/ChunkResidencyTrace.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
static void Environment(const char *key, const std::string &value)
{
#ifdef _WIN32
  _putenv_s(key, value.c_str());
#else
  setenv(key, value.c_str(), 1);
#endif
}
static int SecondaryTest(bool overflow)
{
  const auto base =
      std::filesystem::temp_directory_path() /
      ("cubatarium-residency-pair-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::array<std::filesystem::path, 2> paths = {
      base.string() + "-primary.log", base.string() + "-secondary.log"};
  for (const auto &path : paths)
    Expect(!std::filesystem::exists(path),
           "fresh pair of trace files required");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_START_COLUMN", "1");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_BUFFERED", "1");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_FILE", paths[0].string());
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY", "7,6");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE", paths[1].string());
  cutum::TraceChunkResidency("scratch", {7, 4, 6}, glm::ivec3(0));
  Expect(!cutum::ChunkResidencyTraceSecondaryColumn() &&
             !cutum::TraceChunkResidencyEnabled({7, 4, 6}),
         "secondary waits for runtime selection");
  const int manager = 7;
  cutum::TraceChunkResidencyRuntimeOwner(&manager, {6, -3, 6});
  cutum::SelectChunkResidencyStartColumn({7, -3, 6});
  Expect(cutum::ChunkResidencyTraceColumn() == glm::ivec2(6, 6) &&
             cutum::ChunkResidencyTraceSecondaryColumn() == glm::ivec2(7, 6),
         "two distinct immutable columns");
  const size_t each = overflow ? 1023 : 128;
  std::vector<std::thread> workers;
  for (size_t worker = 0; worker < 4; ++worker)
    workers.emplace_back(
        [&, worker]
        {
          for (size_t n = 0; n < each; ++n)
          {
            cutum::TraceChunkResidency("event", {6 + int(n % 2), 4, 6},
                                       glm::ivec3(0), worker * each + n,
                                       &manager);
            cutum::TraceChunkResidency("excluded", {8, 4, 6}, glm::ivec3(0));
          }
        });
  for (auto &worker : workers)
    worker.join();
  if (overflow)
  {
    for (int x : {6, 7})
      cutum::TraceChunkResidency("event", {x, 4, 6}, glm::ivec3(0),
                                 4092 + x - 6, &manager);
    for (int n = 0; n < 9; ++n)
      cutum::TraceChunkResidency("overflow", {7, 4, 6}, glm::ivec3(0));
  }
  for (const auto &path : paths)
    Expect(!std::filesystem::exists(path),
           "no worker file IO for either column");
  cutum::FlushChunkResidencyTrace();
  size_t total = 0;
  std::set<uint64_t> values;
  std::array<uintmax_t, 2> bytes{};
  for (size_t index = 0; index < paths.size(); ++index)
  {
    std::ifstream file(paths[index]);
    std::string line, summary;
    size_t rows = 0, owners = 0;
    const auto coord = "coord=(" + std::to_string(6 + index) + ",";
    while (std::getline(file, line))
      if (line.find("[ChunkResidency] ") == 0)
      {
        Expect(summary.empty(), "no events after summary");
        Expect(std::stoull(line.substr(line.find("seq=") + 4)) == ++rows,
               "each file has contiguous local sequences");
        Expect(line.find(coord) != std::string::npos,
               "no foreign column in either file");
        if (line.find("source=runtime_owner ") != std::string::npos)
        {
          ++owners;
          Expect(line.find("origin=(6,-3,6)") != std::string::npos,
                 "secondary owner retains actual primary starting focus");
        }
        else
        {
          Expect(line.find("source=event ") != std::string::npos,
                 "scratch, foreign and dropped events excluded");
          Expect(
              values.insert(std::stoull(line.substr(line.find("value=") + 6)))
                  .second,
              "concurrent selected events exported exactly once");
        }
      }
      else
      {
        Expect(summary.empty(), "exactly one summary per file");
        summary = line;
      }
    const auto lost = overflow ? 9 : 0;
    Expect(owners == 1 && rows > 1,
           "both files contain runtime owner and events");
    Expect(summary == "[ChunkResidencySummary] buffered=1 reserved=" +
                          std::to_string(rows + lost) +
                          " published=" + std::to_string(rows) +
                          " dropped=" + std::to_string(lost) +
                          " unpublished=0 complete=" + (overflow ? "0" : "1"),
           "shared overflow invalidates both files even when only secondary "
           "loses events");
    total += rows;
    file.close();
    bytes[index] = std::filesystem::file_size(paths[index]);
  }
  Expect(total == (overflow ? 4096 : 514) && values.size() == total - 2,
         "secondary shares original total capacity rather than doubling it");
  for (int x : {6, 7})
    cutum::TraceChunkResidency("late", {x, 4, 6}, glm::ivec3(0));
  cutum::FlushChunkResidencyTrace();
  for (size_t index = 0; index < paths.size(); ++index)
  {
    Expect(std::filesystem::file_size(paths[index]) == bytes[index],
           "neither closed file can change");
    std::filesystem::remove(paths[index]);
  }
  std::cout << "chunk_residency_trace_secondary_test: OK\n";
  return 0;
}
int main(int argc, char **argv)
{
  if (argc > 1 && (std::string(argv[1]) == "secondary" ||
                   std::string(argv[1]) == "secondary-overflow"))
    return SecondaryTest(std::string(argv[1]) == "secondary-overflow");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY", "");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE", "");
  const bool overflow = argc > 1 && std::string(argv[1]) == "overflow";
  const bool start = argc > 1 && std::string(argv[1]) == "start";
  const auto path =
      std::filesystem::temp_directory_path() /
      ("cubatarium-residency-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".log");
  Expect(!std::filesystem::exists(path), "fresh trace file required");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE", "6,6");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_START_COLUMN", start ? "1" : "0");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_BUFFERED", "1");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_FILE", path.string());
  if (start)
  {
    cutum::TraceChunkResidency("startup_scratch", {6, 4, 6}, glm::ivec3(0));
    cutum::TraceChunkResidency("startup_zero", {0, 4, 0}, glm::ivec3(0));
    Expect(!cutum::TraceChunkResidencyEnabled({6, 4, 6}) &&
               !cutum::TraceChunkResidencyEnabled({0, 4, 0}),
           "start selection waits for runtime owner");
    cutum::SelectChunkResidencyStartColumn({6, -3, 6});
    cutum::SelectChunkResidencyStartColumn({7, -3, 6});
    Expect(cutum::TraceChunkResidencyEnabled({6, 4, 6}) &&
               !cutum::TraceChunkResidencyEnabled({7, 4, 6}),
           "runtime selection is immutable across later callers");
  }
  const size_t each = overflow ? 1024 : 128;
  const int manager = 7;
  std::vector<std::thread> workers;
  for (size_t worker = 0; worker < 4; ++worker)
    workers.emplace_back(
        [&, worker]
        {
          for (size_t n = 0; n < each; ++n)
          {
            cutum::TraceChunkResidency("event", {6, 4, 6}, glm::ivec3(0),
                                       worker * each + n, &manager);
            cutum::TraceChunkResidency("excluded", {7, 4, 6}, glm::ivec3(0));
          }
        });
  for (auto &worker : workers)
    worker.join();
  if (overflow)
    for (int n = 0; n < 9; ++n)
      cutum::TraceChunkResidency("overflow", {6, 4, 6}, glm::ivec3(0));
  Expect(!std::filesystem::exists(path),
         "buffered recording performs no file IO");
  cutum::FlushChunkResidencyTrace();
  std::ifstream file(path);
  std::string line, summary;
  std::set<uint64_t> sequences, values;
  size_t rows = 0;
  while (std::getline(file, line))
    if (line.find("[ChunkResidency] ") == 0)
    {
      const auto sequence = std::stoull(line.substr(line.find("seq=") + 4));
      const auto value = std::stoull(line.substr(line.find("value=") + 6));
      Expect(line.find("source=event coord=(6,4,6)") != std::string::npos,
             "only selected records are exported");
      Expect(sequences.insert(sequence).second && values.insert(value).second,
             "concurrent records export exactly once");
      ++rows;
    }
    else
    {
      Expect(summary.empty(), "one export summary");
      summary = line;
    }
  const size_t count = each * 4;
  Expect(rows == count && *sequences.begin() == 1 &&
             *sequences.rbegin() == count && *values.begin() == 0 &&
             *values.rbegin() == count - 1,
         "all admitted concurrent events retained");
  const std::string expected =
      "[ChunkResidencySummary] buffered=1 reserved=" +
      std::to_string(count + (overflow ? 9 : 0)) +
      " published=" + std::to_string(count) +
      " dropped=" + (overflow ? "9" : "0") +
      " unpublished=0 complete=" + (overflow ? "0" : "1");
  Expect(summary == expected,
         "overflow is explicit and cannot certify complete evidence");
  file.close();
  const auto bytes = std::filesystem::file_size(path);
  cutum::TraceChunkResidency("late", {6, 4, 6}, glm::ivec3(0));
  cutum::FlushChunkResidencyTrace();
  Expect(std::filesystem::file_size(path) == bytes,
         "closed export cannot append or overwrite evidence");
  std::filesystem::remove(path);
  std::cout << "chunk_residency_trace_test: OK\n";
}
