#include "App/Diagnostics/DiagnosticFlightController.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <utility>

namespace cutum
{
namespace
{
float TurnToward(float current, float target, float maximum)
{
  return current + std::clamp(std::remainder(target - current, 360.0f),
                              -maximum, maximum);
}
constexpr float kArrive = 0.65f;
constexpr float kHorizontalMotorDeadZone = 0.35f;
bool Arrived(const DiagnosticFlightWaypoint &point, glm::vec3 position)
{
  const glm::vec3 delta = point.position - position;
  // Panoramas tolerate the general arrival sphere. Directed observations of
  // voxel openings also need horizontal convergence, or the near lip can hide
  // an interior face. Match the normal forward-motor dead zone so this cannot
  // demand an unreachable precision or substitute a camera relocation.
  return glm::length(delta) < kArrive &&
         (!point.inspect || point.inspectionTargets.empty() ||
          glm::length(glm::vec2(delta.x, delta.z)) <= kHorizontalMotorDeadZone);
}
constexpr int kGridRadius = 8;
int Key(glm::ivec3 p)
{
  return (p.x + kGridRadius) +
         17 * ((p.y + kGridRadius) + 17 * (p.z + kGridRadius));
}
} // namespace

void DiagnosticFlightController::SetRoute(
    std::vector<DiagnosticFlightWaypoint> route, bool relative)
{
  *this = DiagnosticFlightController{};
  Route_ = std::move(route);
  Relative_ = relative;
}

bool DiagnosticFlightController::Plan(glm::vec3 from, glm::vec3 target,
                                      const SegmentProbe &probe, bool &unknown)
{
  ++PlanAttempts_;
  Detour_.clear();
  DetourIndex_ = 0;
  const glm::vec3 delta = target - from;
  const float length = glm::length(delta);
  const glm::vec3 goal = from + delta * (std::min(length, 12.0f) / length);
  struct Node
  {
    glm::ivec3 p;
    float cost;
    int parent;
  };
  std::vector<Node> nodes{{glm::ivec3(0), 0.0f, -1}};
  std::unordered_map<int, float> best{{Key(glm::ivec3(0)), 0.0f}};
  using Candidate = std::pair<float, int>;
  std::priority_queue<Candidate, std::vector<Candidate>,
                      std::greater<Candidate>>
      open;
  open.emplace(glm::length(goal - from), 0);
  const glm::ivec3 steps[] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  int expanded = 0;
  while (!open.empty() && expanded < 256)
  {
    const int index = open.top().second;
    open.pop();
    const Node node = nodes[static_cast<size_t>(index)];
    if (node.cost > best[Key(node.p)])
      continue;
    ++expanded;
    ++PlanExpansions_;
    const glm::vec3 position = from + glm::vec3(node.p) * 2.0f;
    if (glm::length(goal - position) <= 4.0f)
    {
      const auto finish = probe(position, goal);
      unknown = unknown || finish == DiagnosticSegmentState::Unknown;
      if (finish == DiagnosticSegmentState::Clear)
      {
        Detour_.push_back(goal);
        for (int at = index; at > 0; at = nodes[static_cast<size_t>(at)].parent)
          Detour_.push_back(from +
                            glm::vec3(nodes[static_cast<size_t>(at)].p) * 2.0f);
        std::reverse(Detour_.begin(), Detour_.end());
        return true;
      }
    }
    for (glm::ivec3 step : steps)
    {
      const glm::ivec3 next = node.p + step;
      if (std::max({std::abs(next.x), std::abs(next.y), std::abs(next.z)}) >
          kGridRadius)
        continue;
      const float cost = node.cost + 2.0f;
      const auto previous = best.find(Key(next));
      if (previous != best.end() && previous->second <= cost)
        continue;
      const glm::vec3 next_position = from + glm::vec3(next) * 2.0f;
      const auto state = probe(position, next_position);
      unknown = unknown || state == DiagnosticSegmentState::Unknown;
      if (state != DiagnosticSegmentState::Clear)
        continue;
      best[Key(next)] = cost;
      nodes.push_back({next, cost, index});
      open.emplace(cost + glm::length(goal - next_position),
                   static_cast<int>(nodes.size()) - 1);
    }
  }
  return false;
}

DiagnosticFlightCommand
DiagnosticFlightController::Tick(const DiagnosticFlightObservation &o,
                                 const SegmentProbe &probe)
{
  DiagnosticFlightCommand command;
  command.yaw = o.yaw;
  command.pitch = o.pitch;
  if (State_ == DiagnosticFlightState::Idle)
  {
    if (Relative_)
      for (auto &point : Route_)
      {
        point.position += o.position;
        for (auto &target : point.inspectionTargets)
          target += o.position;
      }
    LastTick_ = StageStarted_ = LastProgress_ = o.time;
    ProgressPosition_ = o.position;
    TakeoffY_ = o.position.y;
    const bool needs_departure =
        !Route_.empty() && !Arrived(Route_.front(), o.position);
    State_ = o.grounded && needs_departure ? DiagnosticFlightState::Takeoff
                                           : DiagnosticFlightState::Travel;
  }
  const float dt =
      static_cast<float>(std::clamp(o.time - LastTick_, 0.0, 0.25));
  LastTick_ = o.time;
  const auto fail = [&](const char *reason)
  {
    State_ = DiagnosticFlightState::Blocked;
    FailureReason_ = reason;
  };
  if (State_ == DiagnosticFlightState::Blocked ||
      State_ == DiagnosticFlightState::Complete)
    return command;
  if (Waypoint_ >= Route_.size())
  {
    State_ = DiagnosticFlightState::Complete;
    return command;
  }
  if (State_ == DiagnosticFlightState::Takeoff)
  {
    command.pitch = TurnToward(o.pitch, 0.0f, dt * 90.0f);
    // Jump with the walking motor first. The flight motor intentionally
    // lands before processing UP while the capsule still touches ground.
    command.flying = false;
    // Walking anchors the feet before accepting an edge-triggered jump. Drop
    // a stale held UP and wait for actual motor feedback, rather than consume
    // the only jump edge while collision/pose initialization is still pending.
    command.up = o.walkingReady && o.time - StageStarted_ >= 0.1;
    if (!o.grounded && o.position.y >= TakeoffY_ + 0.5f)
    {
      State_ = DiagnosticFlightState::Travel;
      command.flying = true;
      LastProgress_ = o.time;
      ProgressPosition_ = o.position;
    }
    else if (o.time - StageStarted_ > 4.0)
    {
      fail(o.walkingReady ? "takeoff_blocked" : "walking_pose_not_ready");
      command.up = false;
    }
    return command;
  }
  if (State_ == DiagnosticFlightState::Inspect)
  {
    if (!o.captureReady)
      return command;
    float target_yaw =
        InspectYaw_ + 90.0f * static_cast<float>(InspectPose_ / 3);
    float target_pitch = -45.0f + 45.0f * static_cast<float>(InspectPose_ % 3);
    if (InspectPose_ >= 12)
    {
      const glm::vec3 delta =
          Route_[Waypoint_]
              .inspectionTargets[static_cast<size_t>(InspectPose_ - 12)] -
          o.position;
      const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
      target_yaw = horizontal > 0.001f
                       ? glm::degrees(std::atan2(delta.z, delta.x))
                       : o.yaw;
      target_pitch = std::clamp(glm::degrees(std::atan2(delta.y, horizontal)),
                                -89.0f, 89.0f);
    }
    command.yaw = TurnToward(o.yaw, target_yaw, dt * 180.0f);
    command.pitch = TurnToward(o.pitch, target_pitch, dt * 90.0f);
    if (std::abs(std::remainder(o.yaw - target_yaw, 360.0f)) < 2.0f &&
        std::abs(o.pitch - target_pitch) < 2.0f)
    {
      if (InspectPoseStarted_ < 0.0)
      {
        InspectPoseStarted_ = o.time;
        command.capture = true;
        command.captureId = InspectionCaptures_++;
      }
      if (o.time - InspectPoseStarted_ >= 0.35)
      {
        ++InspectPose_;
        InspectPoseStarted_ = -1.0;
        if (static_cast<size_t>(InspectPose_) ==
            12 + Route_[Waypoint_].inspectionTargets.size())
        {
          ++Waypoint_;
          State_ = DiagnosticFlightState::Travel;
          LastProgress_ = o.time;
          ProgressPosition_ = o.position;
        }
      }
    }
    return command;
  }
  if (glm::length(o.position - ProgressPosition_) > 0.25f)
  {
    ProgressPosition_ = o.position;
    LastProgress_ = o.time;
  }
  if (State_ == DiagnosticFlightState::WaitingForTerrain && o.time < NextPlan_)
    return command;
  const glm::vec3 target = Route_[Waypoint_].position;
  if (Arrived(Route_[Waypoint_], o.position))
  {
    Detour_.clear();
    if (Route_[Waypoint_].inspect)
    {
      State_ = DiagnosticFlightState::Inspect;
      InspectYaw_ = o.yaw;
      InspectPose_ = 0;
      InspectPoseStarted_ = -1.0;
    }
    else
      ++Waypoint_;
    LastProgress_ = o.time;
    return command;
  }
  if (o.grounded && target.y > o.position.y + kArrive)
  {
    State_ = DiagnosticFlightState::Takeoff;
    TakeoffY_ = o.position.y;
    StageStarted_ = o.time;
    command.flying = false;
    return command;
  }
  while (DetourIndex_ < Detour_.size() &&
         glm::length(Detour_[DetourIndex_] - o.position) < kArrive)
    ++DetourIndex_;
  glm::vec3 aim =
      DetourIndex_ < Detour_.size() ? Detour_[DetourIndex_] : target;
  const glm::vec3 delta = aim - o.position;
  const float length = glm::length(delta);
  const auto segment =
      probe(o.position, o.position + delta * (std::min(length, 2.0f) / length));
  if (segment != DiagnosticSegmentState::Clear)
  {
    if (o.time >= NextPlan_)
    {
      bool unknown = segment == DiagnosticSegmentState::Unknown;
      const bool found = Plan(o.position, target, probe, unknown);
      NextPlan_ = o.time + 1.0;
      if (!found)
      {
        if (State_ != DiagnosticFlightState::WaitingForTerrain)
          StageStarted_ = o.time;
        State_ = DiagnosticFlightState::WaitingForTerrain;
        if (o.time - StageStarted_ >= (unknown ? 15.0 : 4.0))
          fail(unknown ? "streaming_dependency_timeout" : "no_safe_local_path");
      }
      else
        State_ = DiagnosticFlightState::Travel;
    }
    return command;
  }
  if (State_ == DiagnosticFlightState::WaitingForTerrain)
  {
    LastProgress_ = o.time;
    ProgressPosition_ = o.position;
  }
  State_ = DiagnosticFlightState::Travel;
  if (o.time - LastProgress_ > 5.0)
  {
    fail("movement_stalled");
    return command;
  }
  const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
  const float yaw = horizontal > kHorizontalMotorDeadZone
                        ? glm::degrees(std::atan2(delta.z, delta.x))
                        : o.yaw;
  command.yaw = TurnToward(o.yaw, yaw, dt * 180.0f);
  command.pitch = TurnToward(o.pitch, 0.0f, dt * 90.0f);
  const bool level = std::abs(o.pitch) < 5.0f;
  command.forward = level && horizontal > kHorizontalMotorDeadZone &&
                    std::abs(std::remainder(o.yaw - yaw, 360.0f)) < 10.0f;
  command.up = level && delta.y > 0.35f;
  command.down = level && delta.y < -0.35f;
  return command;
}

const char *DiagnosticFlightController::StateName() const
{
  switch (State_)
  {
  case DiagnosticFlightState::Idle:
    return "idle";
  case DiagnosticFlightState::Takeoff:
    return "takeoff";
  case DiagnosticFlightState::Travel:
    return "travel";
  case DiagnosticFlightState::Inspect:
    return "inspect";
  case DiagnosticFlightState::WaitingForTerrain:
    return "waiting_for_terrain";
  case DiagnosticFlightState::Complete:
    return "complete";
  case DiagnosticFlightState::Blocked:
    return "blocked";
  }
  return "unknown";
}
} // namespace cutum
