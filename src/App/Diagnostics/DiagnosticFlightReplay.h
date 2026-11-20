#pragma once

#include "App/Diagnostics/DiagnosticFlightController.h"
#include <cmath>
#include <istream>
#include <nlohmann/json.hpp>
#include <ostream>
#include <stdexcept>

namespace cutum
{
namespace diagnostic_replay
{
using Json = nlohmann::json;
inline Json Vector(glm::vec3 p) { return Json::array({p.x, p.y, p.z}); }
inline glm::vec3 ReadVector(const Json &value)
{
  if (!value.is_array() || value.size() != 3)
    throw std::runtime_error("invalid replay vector");
  glm::vec3 result;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (!value[axis].is_number())
      throw std::runtime_error("invalid replay coordinate");
    result[axis] = value[axis].get<float>();
    if (!std::isfinite(result[axis]))
      throw std::runtime_error("nonfinite replay coordinate");
  }
  return result;
}
inline Json Snapshot(const DiagnosticFlightController &bot,
                     const DiagnosticFlightCommand &command)
{
  return {{"state", bot.StateName()}, {"reason", bot.FailureReason()},
          {"visited", bot.VisitedWaypoints()}, {"attempts", bot.PlanAttempts()},
          {"expansions", bot.PlanExpansions()},
          {"captures", bot.InspectionCaptureCount()},
          {"command", {{"flying", command.flying}, {"forward", command.forward},
                       {"up", command.up}, {"down", command.down},
                       {"capture", command.capture}, {"capture_id", command.captureId},
                       {"yaw", command.yaw}, {"pitch", command.pitch}}}};
}

// Diagnostic-only bounded stream. No world or camera writer is exposed.
class Recorder
{
public:
  Recorder(std::ostream &output, const Json &route) : Output_(output)
  {
    Write({{"schema", "diagnostic-flight-replay.v1"}, {"kind", "header"},
           {"route", route}});
  }
  DiagnosticFlightCommand Tick(DiagnosticFlightController &bot,
                               const DiagnosticFlightObservation &o,
                               const DiagnosticFlightController::SegmentProbe &probe)
  {
    if (Finished_ || Ticks_ >= 200000)
      throw std::runtime_error("replay tick limit or finalized recorder");
    Json probes = Json::array();
    const auto command = bot.Tick(o, [&](glm::vec3 from, glm::vec3 to)
    {
      if (probes.size() >= 4096)
        throw std::runtime_error("replay probe limit");
      const auto state = probe(from, to);
      probes.push_back({{"from", Vector(from)}, {"to", Vector(to)},
                        {"state", static_cast<int>(state)}});
      return state;
    });
    Write({{"kind", "tick"}, {"sequence", Ticks_++},
           {"observation", {{"position", Vector(o.position)}, {"yaw", o.yaw},
                            {"pitch", o.pitch}, {"time", o.time},
                            {"grounded", o.grounded}, {"walking_ready", o.walkingReady},
                            {"capture_ready", o.captureReady}}},
           {"probes", std::move(probes)}, {"result", Snapshot(bot, command)}});
    return command;
  }
  void Finish()
  {
    if (Finished_)
      throw std::runtime_error("replay already finalized");
    Write({{"kind", "footer"}, {"ticks", Ticks_}});
    Output_.flush();
    if (!Output_)
      throw std::runtime_error("replay flush failed");
    Finished_ = true;
  }
private:
  void Write(const Json &row)
  {
    const auto text = row.dump();
    Bytes_ += text.size() + 1;
    if (text.size() > 2 * 1024 * 1024 || Bytes_ > 64 * 1024 * 1024)
      throw std::runtime_error("replay byte limit");
    Output_ << text << '\n';
    if (!Output_)
      throw std::runtime_error("replay write failed");
  }
  std::ostream &Output_;
  size_t Ticks_{0}, Bytes_{0};
  bool Finished_{false};
};

inline size_t Verify(std::istream &input)
{
  size_t bytes = 0;
  const auto read = [&]()
  {
    // Bound before allocation, including a corrupt file with no newline.
    std::string line;
    char c;
    while (input.get(c))
    {
      if (++bytes > 64 * 1024 * 1024 || line.size() >= 2 * 1024 * 1024)
        throw std::runtime_error("replay input limit");
      if (c == '\n')
        return Json::parse(line);
      line.push_back(c);
    }
    throw std::runtime_error("missing replay row or newline");
  };
  const auto header = read();
  if (header.at("schema") != "diagnostic-flight-replay.v1" || header.at("kind") != "header")
    throw std::runtime_error("invalid replay header");
  const auto &route = header.at("route");
  const auto &points = route.at("waypoints");
  if (route.at("schema") != "diagnostic-flight-route.v1" || !points.is_array() || points.size() > 256)
    throw std::runtime_error("invalid replay route");
  std::vector<DiagnosticFlightWaypoint> waypoints;
  for (const auto &point : points)
  {
    DiagnosticFlightWaypoint waypoint;
    waypoint.position = ReadVector(point.at("position"));
    waypoint.inspect = point.value("inspect", true);
    if (point.contains("inspection_targets"))
    {
      const auto &targets = point.at("inspection_targets");
      if (!targets.is_array() || targets.size() > 8)
        throw std::runtime_error("invalid replay targets");
      for (const auto &target : targets)
        waypoint.inspectionTargets.push_back(ReadVector(target));
    }
    waypoints.push_back(std::move(waypoint));
  }
  const auto mode = route.value("movement_mode", "fly");
  if (mode != "fly" && mode != "walk")
    throw std::runtime_error("invalid replay motor mode");
  DiagnosticFlightController bot;
  bot.SetRoute(std::move(waypoints), route.value("relative", false), mode == "fly");
  size_t ticks = 0;
  double last_time = -1;
  while (true)
  {
    const auto row = read();
    if (row.at("kind") == "footer")
    {
      if (ticks == 0 || !row.at("ticks").is_number_unsigned() || row.at("ticks").get<size_t>() != ticks ||
          input.peek() != std::char_traits<char>::eof() || input.bad())
        throw std::runtime_error("incomplete replay or trailing data");
      return ticks;
    }
    if (ticks >= 200000 || row.at("kind") != "tick" ||
        !row.at("sequence").is_number_unsigned() || row.at("sequence").get<size_t>() != ticks)
      throw std::runtime_error("replay tick sequence mismatch");
    const auto &value = row.at("observation");
    DiagnosticFlightObservation o;
    o.position = ReadVector(value.at("position"));
    o.yaw = value.at("yaw").get<float>();
    o.pitch = value.at("pitch").get<float>();
    o.time = value.at("time").get<double>();
    o.grounded = value.at("grounded").get<bool>();
    o.walkingReady = value.at("walking_ready").get<bool>();
    o.captureReady = value.at("capture_ready").get<bool>();
    if (!std::isfinite(o.yaw) || !std::isfinite(o.pitch) || !std::isfinite(o.time) || o.time < last_time)
      throw std::runtime_error("invalid replay observation");
    last_time = o.time;
    const auto &probes = row.at("probes");
    if (!probes.is_array() || probes.size() > 4096)
      throw std::runtime_error("invalid replay probes");
    size_t next = 0;
    const auto command = bot.Tick(o, [&](glm::vec3 from, glm::vec3 to)
    {
      if (next >= probes.size())
        throw std::runtime_error("missing replay probe");
      const auto &probe = probes[next++];
      if (from != ReadVector(probe.at("from")) || to != ReadVector(probe.at("to")) ||
          !probe.at("state").is_number_integer())
        throw std::runtime_error("replay probe input mismatch");
      const int state = probe.at("state").get<int>();
      if (state < 0 || state > 2)
        throw std::runtime_error("invalid replay probe state");
      return static_cast<DiagnosticSegmentState>(state);
    });
    if (next != probes.size() || Snapshot(bot, command) != row.at("result"))
      throw std::runtime_error("replay decision mismatch");
    ++ticks;
  }
}
} // namespace diagnostic_replay
} // namespace cutum
