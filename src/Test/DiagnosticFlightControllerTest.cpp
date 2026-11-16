#include "App/Diagnostics/DiagnosticFlightController.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace cutum;

static void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

static DiagnosticSegmentState Clear(glm::vec3, glm::vec3)
{
  return DiagnosticSegmentState::Clear;
}

static void Step(DiagnosticFlightController &bot,
                 DiagnosticFlightObservation &o,
                 const DiagnosticFlightController::SegmentProbe &probe)
{
  o.time += 0.05;
  const auto action = bot.Tick(o, probe);
  o.yaw = action.yaw;
  o.pitch = action.pitch;
  const glm::vec3 before = o.position;
  if (action.forward)
    o.position += glm::vec3(std::cos(glm::radians(action.yaw)), 0.0f,
                            std::sin(glm::radians(action.yaw))) *
                  0.15f;
  if (action.up)
    o.position.y += 0.15f;
  if (action.down)
    o.position.y -= 0.15f;
  if (before != o.position)
    Expect(probe(before, o.position) == DiagnosticSegmentState::Clear,
           "controller tried moving into a blocked or unknown segment");
}

int main()
{
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{0, -48, 0}, 0, 0, 0, true};
    const auto jump = bot.Tick(o, Clear);
    Expect(!jump.flying && jump.up && !jump.forward,
           "ground takeoff must first jump with the walking motor");
    o.position.y += 0.6f;
    o.grounded = false;
    o.time = 0.1;
    Expect(bot.Tick(o, Clear).flying, "airborne jump may enter flight");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, true};
    for (int i = 0; i < 120; ++i)
    {
      o.time += 0.05;
      bot.Tick(o, Clear);
    }
    Expect(bot.State() == DiagnosticFlightState::Blocked &&
               bot.FailureReason() == "takeoff_blocked",
           "a ceiling must not cause infinite jump attempts");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{8, 2, 0}, false}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    const auto wall = [](glm::vec3 from, glm::vec3 to)
    {
      for (int i = 1; i <= 32; ++i)
      {
        const glm::vec3 p =
            from + (to - from) * (static_cast<float>(i) / 32.0f);
        if (p.x > 3.0f && p.x < 4.5f && p.y < 4.5f)
          return DiagnosticSegmentState::Blocked;
      }
      return DiagnosticSegmentState::Clear;
    };
    for (int i = 0; i < 1200 && bot.State() != DiagnosticFlightState::Complete;
         ++i)
      Step(bot, o, wall);
    Expect(bot.State() == DiagnosticFlightState::Complete,
           "3D local planner must fly over a wall rather than relocate");
    Expect(bot.PlanAttempts() > 0 &&
               bot.PlanExpansions() <= bot.PlanAttempts() * 256,
           "planner expansion budget must be enforced");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{8, 2, 0}, false}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    const auto unknown = [](glm::vec3, glm::vec3)
    { return DiagnosticSegmentState::Unknown; };
    for (int i = 0; i < 400; ++i)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, unknown);
      Expect(!action.forward && !action.up && !action.down,
             "unknown terrain must not be traversed");
    }
    Expect(bot.State() == DiagnosticFlightState::Blocked &&
               bot.FailureReason() == "streaming_dependency_timeout",
           "unknown terrain needs a bounded explicit failure");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 2, 0}, true}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    for (int i = 0; i < 1000 && bot.State() != DiagnosticFlightState::Complete;
         ++i)
      Step(bot, o, Clear);
    Expect(bot.State() == DiagnosticFlightState::Complete &&
               bot.VisitedWaypoints() == 1,
           "stationary twelve-direction inspection must finish");
    Expect(o.position == glm::vec3(0, 2, 0),
           "inspection must not move the camera");
    Expect(bot.InspectionCaptureCount() == 12,
           "each stable inspection direction must request its own capture");
  }
  return EXIT_SUCCESS;
}
