#include "World/Diagnostics/CreatureMovementDiagnostics.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

// Declared in App/Core.h; resolved by the main app (or a unit-test stub).
std::filesystem::path GetExecutableDirectory();

namespace
{

using json = nlohmann::json;

constexpr size_t kRingCapacity = 512;

std::atomic<bool> &EnabledFlag()
{
  static std::atomic<bool> enabled{false};
  return enabled;
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

std::optional<std::filesystem::path> &PathOverride()
{
  static std::optional<std::filesystem::path> override_path;
  return override_path;
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

bool AppendJsonLine(const std::filesystem::path &path, const json &record)
{
  std::ofstream out(path, std::ios::app);
  if (!out.is_open())
  {
    return false;
  }
  out << record.dump() << '\n';
  return out.good();
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
    RingBuffer().clear();
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

int UCreatureMovementDiagnostics::Record(const CreatureMovementDiagRecord &record)
{
  if (!IsEnabled())
  {
    return -1;
  }
  const uint64_t focus = GetFocusId();
  if (focus != 0 && record.creatureId != focus)
  {
    return -1;
  }
  if (record.event.empty())
  {
    return -1;
  }

  const int sample_index =
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

  {
    std::lock_guard<std::mutex> lock(StateMutex());
    auto &ring = RingBuffer();
    ring.push_back(line);
    while (ring.size() > kRingCapacity)
    {
      ring.pop_front();
    }
  }

  AppendJsonLine(DefaultLogPath(), line);
  return sample_index;
}

bool UCreatureMovementDiagnostics::DumpRing(const std::filesystem::path &path)
{
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
