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
    DiagnosticFlightWaypoint point{{22, -44.88f, 51}, true};
    point.inspectionTargets = {{22, -49.5f, 51}, {21.5f, -49.1f, 51}};
    bot.SetRoute({point}, false);
    // Retained AIR-hole native return stopped here under the general 0.65
    // arrival sphere, outside the one-voxel opening. The left wall was hidden.
    DiagnosticFlightObservation o{{21.4832f, -44.6193f, 51}, 0, 0, 0, false};
    const auto approach = bot.Tick(o, Clear);
    Expect(bot.State() == DiagnosticFlightState::Travel && approach.forward &&
               !approach.capture,
           "targeted inspection must approach within the horizontal motor "
           "dead zone before capturing a one-voxel opening");
    for (int tick = 0;
         tick < 1600 && bot.State() != DiagnosticFlightState::Complete; ++tick)
      Step(bot, o, Clear);
    Expect(bot.State() == DiagnosticFlightState::Complete &&
               std::abs(o.position.x - 22) <= .35f &&
               bot.InspectionCaptureCount() == 14,
           "targeted inspection must converge by motor without stalling or "
           "losing its panorama and target captures");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 0, 0}, true}, {{3, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{22, -46.88f, 51}, 180, 0, 0, true, true};
    for (int tick = 0; tick < 800 && bot.VisitedWaypoints() == 0; ++tick)
    {
      o.time += .05;
      const auto action = bot.Tick(o, Clear);
      Expect(!action.up && !action.down && !action.forward,
             "grounded observation must not jump or move before inspection "
             "completes");
      o.yaw = action.yaw;
      o.pitch = action.pitch;
    }
    Expect(bot.VisitedWaypoints() == 1 && bot.InspectionCaptureCount() == 12,
           "grounded first waypoint completes its actual stable panorama");
    o.time += .05;
    const auto depart = bot.Tick(o, Clear);
    Expect(
        bot.State() == DiagnosticFlightState::Takeoff && !depart.flying,
        "departing the grounded inspection starts the normal walking takeoff");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({}, true);
    DiagnosticFlightObservation o{{0, -48, 0}, 0, 0, 0, true};
    const auto action = bot.Tick(o, Clear);
    Expect(
        bot.State() == DiagnosticFlightState::Complete && !action.forward &&
            !action.up && !action.down && !action.capture &&
            bot.VisitedWaypoints() == 0,
        "empty startup observation must complete without movement or capture");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{0, -48, 0}, 0, 0, 0, true};
    const auto jump = bot.Tick(o, Clear);
    Expect(
        !jump.flying && !jump.up && !jump.forward,
        "takeoff enters walking with jump released until the motor is ready");
    o.walkingReady = true;
    o.time = 0.11;
    Expect(bot.Tick(o, Clear).up,
           "a ready walking motor may receive a fresh jump press");
    o.position.y += 0.6f;
    o.grounded = false;
    o.time = 0.2;
    Expect(bot.Tick(o, Clear).flying, "airborne jump may enter flight");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, true};
    o.walkingReady = true;
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
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 2, 0}, true}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 12, -45, 0, false};
    bot.Tick(o, Clear);
    o.captureReady = false;
    for (int i = 0; i < 400; ++i)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, Clear);
      Expect(!action.capture && !action.forward && !action.up && !action.down &&
                 action.yaw == o.yaw && action.pitch == o.pitch,
             "full capture pipeline must hold pose without issuing IDs or "
             "movement");
    }
    Expect(bot.InspectionCaptureCount() == 0 && bot.VisitedWaypoints() == 0,
           "encoder backpressure must not complete an unrecorded inspection");
    o.captureReady = true;
    bool blocked_after_capture = false;
    for (int i = 0; i < 1400 && bot.State() != DiagnosticFlightState::Complete;
         ++i)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, Clear);
      o.yaw = action.yaw;
      o.pitch = action.pitch;
      if (action.capture && !blocked_after_capture)
      {
        Expect(action.captureId == 0, "first admitted capture retains ID zero");
        const auto count = bot.InspectionCaptureCount();
        const auto yaw = o.yaw;
        const auto pitch = o.pitch;
        o.captureReady = false;
        for (int stalled = 0; stalled < 100; ++stalled)
        {
          o.time += .05;
          const auto held = bot.Tick(o, Clear);
          Expect(
              !held.capture && held.yaw == yaw && held.pitch == pitch &&
                  bot.InspectionCaptureCount() == count,
              "pending capture must not be duplicated or lose its stable pose");
        }
        o.captureReady = true;
        blocked_after_capture = true;
      }
    }
    Expect(blocked_after_capture &&
               bot.State() == DiagnosticFlightState::Complete &&
               bot.InspectionCaptureCount() == 12,
           "inspection resumes after capacity recovery and captures every view "
           "once");
  }

  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 0, 0}, true, {{2, -7.5f, 0}}}}, true);
    DiagnosticFlightObservation o{{22, -36, 51}, 180, 0, 0, false};
    bool targeted_capture = false;
    for (int i = 0; i < 1200 && bot.State() != DiagnosticFlightState::Complete;
         ++i)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, Clear);
      Expect(!action.forward && !action.up && !action.down,
             "targeted inspection must remain stationary");
      if (action.capture && action.captureId == 12)
      {
        const glm::vec3 direction(std::cos(glm::radians(action.pitch)) *
                                      std::cos(glm::radians(action.yaw)),
                                  std::sin(glm::radians(action.pitch)),
                                  std::cos(glm::radians(action.pitch)) *
                                      std::sin(glm::radians(action.yaw)));
        Expect(glm::dot(direction, glm::normalize(glm::vec3(2, -7.5f, 0))) >
                   0.999f,
               "relative inspection target must translate once and aim at its "
               "world point");
        targeted_capture = true;
      }
      o.yaw = action.yaw;
      o.pitch = action.pitch;
    }
    Expect(targeted_capture && bot.State() == DiagnosticFlightState::Complete &&
               bot.InspectionCaptureCount() == 13,
           "panorama plus target must produce thirteen stable captures and "
           "complete");
  }
  {
    for (const bool previously_held : {false, true})
    {
      DiagnosticFlightController bot;
      bot.SetRoute({{{0, 5, 0}, false}}, true);
      DiagnosticFlightObservation o{{0, -58.88f, 0}, 0, 0, 0, true};
      bool held = previously_held;
      bool jumped = false;
      for (int i = 0; i < 60; ++i)
      {
        o.time += 0.05;
        // Physical standing becomes ready after geometric support is known.
        o.walkingReady = !jumped && i >= 20;
        const auto action = bot.Tick(o, Clear);
        if (!o.walkingReady && o.grounded)
          Expect(!action.up,
                 "pending standing pose must not consume a jump edge");
        if (!action.flying && action.up && !held && o.walkingReady)
        {
          o.position.y += 0.6f;
          o.grounded = false;
          jumped = true;
        }
        held = action.up;
        if (jumped && action.flying)
          break;
      }
      Expect(jumped && bot.State() == DiagnosticFlightState::Travel,
             "fresh jump after delayed motor readiness must enter flight "
             "without relocation");
    }
    DiagnosticFlightController bot;
    bot.SetRoute({{{0, 5, 0}, false}}, true);
    DiagnosticFlightObservation o{{0, -58.88f, 0}, 0, 0, 0, true};
    for (int i = 0; i < 100; ++i)
    {
      o.time += 0.05;
      bot.Tick(o, Clear);
    }
    Expect(bot.State() == DiagnosticFlightState::Blocked &&
               bot.FailureReason() == "walking_pose_not_ready",
           "missing motor readiness must have its own bounded failure");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{8, 2, 0}, false}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    const auto unknown = [](glm::vec3, glm::vec3)
    { return DiagnosticSegmentState::Unknown; };
    for (int tick = 0; tick < 60; ++tick)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, unknown);
      Expect(!action.forward && !action.up && !action.down,
             "temporary Unknown must hold the motor");
    }
    for (int tick = 0; tick < 300 &&
                       bot.State() != DiagnosticFlightState::Complete; ++tick)
      Step(bot, o, Clear);
    Expect(bot.State() == DiagnosticFlightState::Complete,
           "terrain arrival must recover through normal motor movement");
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{8, 2, 0}, false}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    for (int tick = 0; tick < 420 &&
                       bot.State() != DiagnosticFlightState::Blocked; ++tick)
    {
      o.time += 0.05;
      // Streaming availability flickers, but the physical motor never moves.
      const auto probe = [&](glm::vec3, glm::vec3)
      { return (static_cast<int>(o.time) % 2 == 0)
                   ? DiagnosticSegmentState::Unknown
                   : DiagnosticSegmentState::Clear; };
      bot.Tick(o, probe);
    }
    Expect(bot.State() == DiagnosticFlightState::Blocked,
           "availability flicker without physical progress must terminate");
    Expect(bot.PlanAttempts() <= 20 &&
               bot.PlanExpansions() <= bot.PlanAttempts() * 256,
           "non-progress recovery must have bounded planning work");
    for (int tick = 0; tick < 20; ++tick)
    {
      o.time += 0.05;
      const auto action = bot.Tick(o, Clear);
      Expect(bot.State() == DiagnosticFlightState::Blocked &&
                 !action.forward && !action.up && !action.down && !action.capture,
             "terminal blocked state must not resume motor or capture commands");
    }
  }
  {
    for (const bool enclosed : {false, true})
    {
      DiagnosticFlightController bot;
      bot.SetRoute({{{8, 2, 0}, false}}, false);
      DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
      const auto probe = [&](glm::vec3, glm::vec3)
      { return enclosed ? DiagnosticSegmentState::Blocked
                        : DiagnosticSegmentState::Clear; };
      for (int tick = 0; tick < 130 &&
                         bot.State() != DiagnosticFlightState::Blocked; ++tick)
      {
        o.time += 0.05;
        bot.Tick(o, probe);
      }
      Expect(bot.State() == DiagnosticFlightState::Blocked &&
                 bot.FailureReason() == (enclosed ? "no_safe_local_path"
                                                 : "movement_stalled"),
             "enclosure and a stalled motor must have bounded distinct failures");
    }
  }
  {
    DiagnosticFlightController bot;
    bot.SetRoute({{{8, 2, 0}, false}}, false);
    DiagnosticFlightObservation o{{0, 2, 0}, 0, 0, 0, false};
    const auto unknown = [](glm::vec3, glm::vec3)
    { return DiagnosticSegmentState::Unknown; };
    for (int tick = 0; tick < 200; ++tick)
    {
      o.time += 0.05;
      bot.Tick(o, unknown);
    }
    // Real observed progress starts a new bounded recovery interval.
    o.position.x += 0.3f;
    for (int tick = 0; tick < 200; ++tick)
    {
      o.time += 0.05;
      bot.Tick(o, unknown);
    }
    Expect(bot.State() != DiagnosticFlightState::Blocked,
           "actual motor progress must reset the recovery deadline");
    for (int tick = 0; tick < 300 &&
                       bot.State() != DiagnosticFlightState::Complete; ++tick)
      Step(bot, o, Clear);
    Expect(bot.State() == DiagnosticFlightState::Complete,
           "progressing recovery must retain a usable route");
  }
  return EXIT_SUCCESS;
}
