#pragma once

#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace cutum
{

enum class DiagnosticSegmentState
{
  Clear,
  Blocked,
  Unknown
};
enum class DiagnosticFlightState
{
  Idle,
  Takeoff,
  Travel,
  Inspect,
  WaitingForTerrain,
  Complete,
  Blocked
};

struct DiagnosticFlightWaypoint
{
  glm::vec3 position{0.0f};
  bool inspect{true};
};

struct DiagnosticFlightObservation
{
  glm::vec3 position{0.0f};
  float yaw{0.0f};
  float pitch{0.0f};
  double time{0.0};
  bool grounded{false};
};

struct DiagnosticFlightCommand
{
  bool flying{true};
  bool forward{false};
  bool up{false};
  bool down{false};
  float yaw{0.0f};
  float pitch{0.0f};
};

/// Bounded, observable diagnostic driver. It never receives a world writer or
/// camera reference: its only output is normal movement and view intent.
class DiagnosticFlightController
{
public:
  using SegmentProbe =
      std::function<DiagnosticSegmentState(glm::vec3, glm::vec3)>;
  void SetRoute(std::vector<DiagnosticFlightWaypoint> route, bool relative);
  DiagnosticFlightCommand Tick(const DiagnosticFlightObservation &observation,
                               const SegmentProbe &probe);
  DiagnosticFlightState State() const { return State_; }
  const char *StateName() const;
  const std::string &FailureReason() const { return FailureReason_; }
  size_t VisitedWaypoints() const { return Waypoint_; }
  int PlanAttempts() const { return PlanAttempts_; }
  int PlanExpansions() const { return PlanExpansions_; }

private:
  bool Plan(glm::vec3 from, glm::vec3 target, const SegmentProbe &probe,
            bool &unknown);
  std::vector<DiagnosticFlightWaypoint> Route_;
  std::vector<glm::vec3> Detour_;
  size_t Waypoint_{0}, DetourIndex_{0};
  DiagnosticFlightState State_{DiagnosticFlightState::Idle};
  bool Relative_{true};
  double LastTick_{0.0}, StageStarted_{0.0}, LastProgress_{0.0};
  double NextPlan_{0.0}, InspectPoseStarted_{-1.0};
  glm::vec3 ProgressPosition_{0.0f};
  float TakeoffY_{0.0f}, InspectYaw_{0.0f};
  int InspectPose_{0}, PlanAttempts_{0}, PlanExpansions_{0};
  std::string FailureReason_;
};

} // namespace cutum
