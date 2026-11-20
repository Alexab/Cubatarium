#include "App/Diagnostics/DiagnosticFlightReplay.h"
#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace cutum;
using namespace cutum::diagnostic_replay;

static void Expect(bool ok, const char *message)
{
  if (!ok)
  {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}
static std::string Record(bool walking, bool blocked, bool wall = false)
{
  Json route = {{"schema", "diagnostic-flight-route.v1"}, {"relative", true},
                {"movement_mode", walking ? "walk" : "fly"},
                {"waypoints", Json::array({{{"position", {8, 0, 0}}, {"inspect", true},
                                           {"inspection_targets", {{10, -2, 0}}}}})}};
  DiagnosticFlightController bot;
  bot.SetRoute({{{8, 0, 0}, true, {{10, -2, 0}}}}, true, !walking);
  std::ostringstream output;
  Recorder recorder(output, route);
  DiagnosticFlightObservation o{{22, -36, 51}, 0, 0, 0, walking, walking};
  for (int tick = 0; tick < 1200 && bot.State() != DiagnosticFlightState::Complete &&
                     bot.State() != DiagnosticFlightState::Blocked; ++tick)
  {
    o.time += .05;
    o.captureReady = tick % 7 != 0;
    const auto command = recorder.Tick(bot, o, [&](glm::vec3 from, glm::vec3 to)
    {
      if (wall)
        for (int sample = 1; sample <= 32; ++sample)
        {
          const auto p = from + (to-from) * (static_cast<float>(sample)/32);
          if (p.x > 25 && p.x < 26.5f && p.y < -33.5f)
            return DiagnosticSegmentState::Blocked;
        }
      return blocked ? DiagnosticSegmentState::Blocked :
             tick < 40 ? DiagnosticSegmentState::Unknown : DiagnosticSegmentState::Clear;
    });
    o.yaw = command.yaw;
    o.pitch = command.pitch;
    if (command.forward)
      o.position += glm::vec3(std::cos(glm::radians(command.yaw)), 0,
                              std::sin(glm::radians(command.yaw))) * .15f;
    if (command.up) o.position.y += .15f;
    if (command.down) o.position.y -= .15f;
  }
  Expect(bot.State() == (blocked ? DiagnosticFlightState::Blocked : DiagnosticFlightState::Complete),
         "recorded scenario must reach its intended terminal state");
  recorder.Finish();
  return output.str();
}
static void Reject(const std::string &text)
{
  try
  {
    std::istringstream input(text);
    Verify(input);
  }
  catch (const std::exception &) { return; }
  Expect(false, "corrupt replay was accepted");
}
int main()
{
  for (const bool walking : {false, true})
    for (const bool blocked : {false, true})
    {
      const auto text = Record(walking, blocked);
      std::istringstream first(text), second(text);
      Expect(Verify(first) == Verify(second), "recorded decisions must replay identically twice");
    }
  const auto wall = Record(false, false, true);
  std::istringstream wall_first(wall), wall_second(wall);
  Expect(Verify(wall_first) == Verify(wall_second), "multi-node detour probes must replay exactly");
  const auto text = Record(false, false);
  std::vector<Json> rows;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) rows.push_back(Json::parse(line));
  const auto serialize = [](const std::vector<Json> &values)
  {
    std::string result;
    for (const auto &value : values) result += value.dump() + "\n";
    return result;
  };
  auto corrupt = rows;
  corrupt[1]["sequence"] = 1;
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[1]["result"]["command"]["forward"] = true;
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[1]["probes"][0]["from"][0] = 0;
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[1]["probes"].push_back(corrupt[1]["probes"][0]);
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[1]["probes"] = Json::array();
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[1]["probes"][0]["state"] = 3;
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt[2]["observation"]["time"] = -1;
  Reject(serialize(corrupt));
  corrupt = rows;
  corrupt.pop_back();
  Reject(serialize(corrupt));
  Reject(text.substr(0, text.size()-1));
  Reject(text + "{}\n");
  std::ostringstream failed;
  failed.setstate(std::ios::badbit);
  bool rejected = false;
  try { Recorder recorder(failed, rows[0]["route"]); }
  catch (const std::exception &) { rejected = true; }
  Expect(rejected, "failed recording output must not silently succeed");
  std::cout << "diagnostic replay positive and negative controls PASS\n";
  return EXIT_SUCCESS;
}
