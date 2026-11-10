#include "World/Diagnostics/CreatureMovementDiagnostics.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace cutum
{

std::filesystem::path GetExecutableDirectory();

namespace
{

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr size_t kRingCapacity = 512;
constexpr size_t kFlushBatchSize = 32;
constexpr double kFlushIntervalSec = 0.5;
constexpr double kFocusSampleIntervalSec = 0.5;

std::atomic<bool> &EnabledFlag()
{
  static std::atomic<bool> enabled{false};
  return enabled;
}

std::atomic<bool> &VerboseFlag()
{
  static std::atomic<bool> verbose{false};
  return verbose;
}

std::atomic<uint64_t> &FocusIdFlag()
{
  static std::atomic<uint64_t> focus{0};
  return focus;
}

std::atomic<int> &SampleCounter()
{
  static std::atomic<int> counter{0};
  return counter;
}

std::mutex &StateMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::deque<json> &RingBuffer()
{
  static std::deque<json> ring;
  return ring;
}

std::deque<json> &PendingDisk()
{
  static std::deque<json> pending;
  return pending;
}

Clock::time_point &LastFlushTime()
{
  static Clock::time_point t = Clock::now();
  return t;
}

std::unordered_map<uint64_t, Clock::time_point> &LastFocusSample()
{
  static std::unordered_map<uint64_t, Clock::time_point> map;
  return map;
}

std::optional<std::filesystem::path> &PathOverride()
{
  static std::optional<std::filesystem::path> override_path;
  return override_path;
}

bool IsImportantEvent(const std::string &event)
{
  return event == "blocked" || event == "habitat_reject" || event == "stuck" ||
         event == "path_fail" || event == "path_ok";
}

bool IsStreamEvent(const std::string &event)
{
  return event == "intent" || event == "activity_skip";
}

std::string IsoTimestampUtc()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

json Vec3Json(const glm::vec3 &v)
{
  return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

void AppendJsonLines(const std::filesystem::path &path,
                     const std::deque<json> &lines)
{
  if (lines.empty())
  {
    return;
  }
  std::ofstream out(path, std::ios::app);
  if (!out.is_open())
  {
    return;
  }
  for (const json &line : lines)
  {
    out << line.dump() << '\n';
  }
}

void FlushPendingLocked()
{
  if (PendingDisk().empty())
  {
    return;
  }
  std::filesystem::path path;
  if (PathOverride())
  {
    path = *PathOverride();
  }
  else
  {
    path = GetExecutableDirectory() / "creature_movement_diag.jsonl";
  }
  AppendJsonLines(path, PendingDisk());
  PendingDisk().clear();
  LastFlushTime() = Clock::now();
}

bool ShouldRecordStreamLocked(uint64_t creature_id, bool verbose, uint64_t focus)
{
  if (verbose)
  {
    if (focus != 0 && creature_id != focus)
    {
      return false;
    }
  }
  else if (focus == 0 || creature_id != focus)
  {
    // Default: stream samples only for focused creature.
    return false;
  }

  const auto now = Clock::now();
  auto &map = LastFocusSample();
  const auto it = map.find(creature_id);
  if (it != map.end())
  {
    const double elapsed =
        std::chrono::duration<double>(now - it->second).count();
    if (elapsed < kFocusSampleIntervalSec)
    {
      return false;
    }
  }
  map[creature_id] = now;
  return true;
}

} // namespace

void UCreatureMovementDiagnostics::SetEnabled(bool enabled)
{
  EnabledFlag().store(enabled, std::memory_order_relaxed);
}

bool UCreatureMovementDiagnostics::IsEnabled()
{
  return EnabledFlag().load(std::memory_order_relaxed);
}

void UCreatureMovementDiagnostics::SetVerbose(bool verbose)
{
  VerboseFlag().store(verbose, std::memory_order_relaxed);
}

bool UCreatureMovementDiagnostics::IsVerbose()
{
  return VerboseFlag().load(std::memory_order_relaxed);
}

void UCreatureMovementDiagnostics::SetFocusId(uint64_t creature_id)
{
  FocusIdFlag().store(creature_id, std::memory_order_relaxed);
}

uint64_t UCreatureMovementDiagnostics::GetFocusId()
{
  return FocusIdFlag().load(std::memory_order_relaxed);
}

void UCreatureMovementDiagnostics::SetLogPathOverride(
    const std::filesystem::path &path)
{
  std::lock_guard<std::mutex> lock(StateMutex());
  PathOverride() = path;
}

void UCreatureMovementDiagnostics::ClearLogPathOverride()
{
  std::lock_guard<std::mutex> lock(StateMutex());
  PathOverride().reset();
}

std::filesystem::path UCreatureMovementDiagnostics::DefaultLogPath()
{
  {
    std::lock_guard<std::mutex> lock(StateMutex());
    if (PathOverride())
    {
      return *PathOverride();
    }
  }
  return GetExecutableDirectory() / "creature_movement_diag.jsonl";
}

bool UCreatureMovementDiagnostics::ClearLog()
{
  SampleCounter().store(0, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(StateMutex());
    FlushPendingLocked();
    RingBuffer().clear();
    PendingDisk().clear();
    LastFocusSample().clear();
  }
  std::error_code ec;
  std::filesystem::remove(DefaultLogPath(), ec);
  return true;
}

int UCreatureMovementDiagnostics::GetSampleCount()
{
  return SampleCounter().load(std::memory_order_relaxed);
}

int UCreatureMovementDiagnostics::GetRingSize()
{
  std::lock_guard<std::mutex> lock(StateMutex());
  return static_cast<int>(RingBuffer().size());
}

void UCreatureMovementDiagnostics::Flush()
{
  std::lock_guard<std::mutex> lock(StateMutex());
  FlushPendingLocked();
}

int UCreatureMovementDiagnostics::Record(const CreatureMovementDiagRecord &record)
{
  if (!IsEnabled())
  {
    return -1;
  }
  if (record.event.empty())
  {
    return -1;
  }

  const uint64_t focus = GetFocusId();
  const bool verbose = IsVerbose();
  const bool important = IsImportantEvent(record.event);
  const bool is_stream = IsStreamEvent(record.event);

  if (important || !is_stream)
  {
    if (focus != 0 && record.creatureId != 0 && record.creatureId != focus)
    {
      return -1;
    }
  }

  int sample_index = -1;
  {
    std::lock_guard<std::mutex> lock(StateMutex());
    if (is_stream &&
        !ShouldRecordStreamLocked(record.creatureId, verbose, focus))
    {
      return -1;
    }

    sample_index =
        SampleCounter().fetch_add(1, std::memory_order_relaxed) + 1;

    json line;
    line["schema"] = "creature_movement_diag.v1";
    line["sample_index"] = sample_index;
    line["t"] = IsoTimestampUtc();
    line["event"] = record.event;
    line["creatureId"] = record.creatureId;
    if (!record.typeId.empty())
    {
      line["typeId"] = record.typeId;
    }
    if (!record.habitat.empty())
    {
      line["habitat"] = record.habitat;
    }
    if (!record.behavior.empty())
    {
      line["behavior"] = record.behavior;
    }
    if (!record.fsmState.empty())
    {
      line["fsmState"] = record.fsmState;
    }
    if (!record.reason.empty())
    {
      line["reason"] = record.reason;
    }
    if (!record.goalSource.empty())
    {
      line["goalSource"] = record.goalSource;
    }
    line["body"] = Vec3Json(record.body);
    line["intentDir"] = Vec3Json(record.intentDir);
    line["intentSpeed"] = record.intentSpeed;
    line["travel"] = Vec3Json(record.travel);
    line["onGround"] = record.onGround;
    line["inFluid"] = record.inFluid;
    line["pathValid"] = record.pathValid;
    if (record.waypointCount >= 0)
    {
      line["waypointCount"] = record.waypointCount;
    }
    line["activityTick"] = record.activityTick;

    auto &ring = RingBuffer();
    ring.push_back(line);
    while (ring.size() > kRingCapacity)
    {
      ring.pop_front();
    }
    PendingDisk().push_back(line);
    const double since_flush =
        std::chrono::duration<double>(Clock::now() - LastFlushTime()).count();
    // Important events flush immediately so path_fail/habitat_reject are visible.
    if (important || PendingDisk().size() >= kFlushBatchSize ||
        since_flush >= kFlushIntervalSec)
    {
      FlushPendingLocked();
    }
  }
  return sample_index;
}

bool UCreatureMovementDiagnostics::DumpRing(const std::filesystem::path &path)
{
  Flush();
  const std::filesystem::path out_path =
      path.empty() ? (GetExecutableDirectory() / "creature_movement_diag.dump.jsonl")
                   : path;
  std::lock_guard<std::mutex> lock(StateMutex());
  std::ofstream out(out_path, std::ios::trunc);
  if (!out.is_open())
  {
    return false;
  }
  for (const json &line : RingBuffer())
  {
    out << line.dump() << '\n';
  }
  return out.good();
}

} // namespace cutum
