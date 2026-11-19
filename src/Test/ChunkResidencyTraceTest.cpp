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
int main(int argc, char **)
{
  const bool overflow = argc > 1;
  const auto path =
      std::filesystem::temp_directory_path() /
      ("cubatarium-residency-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".log");
  Expect(!std::filesystem::exists(path), "fresh trace file required");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE", "6,6");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_BUFFERED", "1");
  Environment("CUBA_CHUNK_RESIDENCY_TRACE_FILE", path.string());
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
