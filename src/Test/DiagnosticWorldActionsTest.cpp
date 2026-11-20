#include "App/Diagnostics/DiagnosticWorldActions.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace cutum;
using nlohmann::json;
static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
int main()
{
  const auto root =
      std::filesystem::temp_directory_path() /
      ("cubatarium-world-actions-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto clone = root / "EngineSmoke_test";
  const auto original = root / "World_261";
  std::filesystem::create_directories(clone);
  std::filesystem::create_directories(original);
  Expect(DiagnosticWorldStorageMatches(clone / ".", clone),
         "copy storage identity rejected");
  Expect(!DiagnosticWorldStorageMatches(original, clone),
         "source storage admitted as copy");
  Expect(!DiagnosticWorldStorageMatches(root / "missing", clone),
         "missing storage admitted");
  std::filesystem::remove(clone);
  std::filesystem::remove(original);
  std::filesystem::remove(root);
  json route = {{"purpose", "round_trip"},
                {"isolated_world", "EngineSmoke_test"},
                {"world_actions",
                 {{"schema", "engine-native-world-actions.v1"},
                  {"entries", json::array({{{"cell", {24, -44, 51}},
                                            {"expected", "stone"},
                                            {"replacement", "glowstone"}}})}}}};
  const auto parse = [](const json &r)
  { return ParseDiagnosticWorldActions(r, "EngineSmoke_test"); };
  Expect(parse(route).at(0).cell.y == -44, "negative cell lost");
  Expect(DiagnosticWorldActionAfterWaypoints(route.at("world_actions")) == 0,
         "legacy action start gate changed");
  Expect(!DiagnosticWorldActionMayStart(false, false, 1, 1) &&
             !DiagnosticWorldActionMayStart(true, false, 0, 1) &&
             DiagnosticWorldActionMayStart(true, false, 1, 1) &&
             !DiagnosticWorldActionMayStart(true, true, 1, 1),
         "action must await actual completed waypoint and execute once");
  Expect(ParseDiagnosticWorldActions(json::object(), "World_261").empty(),
         "default changed");
  const auto reject = [&](const json &r)
  {
    try
    {
      parse(r);
    }
    catch (const std::exception &)
    {
      return;
    }
    Expect(false, "invalid world action admitted");
  };
  for (const auto &bad : {json(true), json(1.5), json(uint64_t(1) << 63),
                          json(int64_t(-2147483649LL))})
  {
    auto r = route;
    r["world_actions"]["entries"][0]["cell"][0] = bad;
    reject(r);
  }
  for (const auto &bad :
       {json(true), json(1.5), json(-1), json(3), json(uint64_t(1) << 63)})
  {
    auto gated = route;
    gated["world_actions"]["after_waypoints"] = bad;
    reject(gated);
  }
  auto gated = route;
  gated["world_actions"]["after_waypoints"] = 1;
  Expect(parse(gated).size() == 1 && DiagnosticWorldActionAfterWaypoints(
                                         gated.at("world_actions")) == 1,
         "explicit post-inspection action rejected");
  auto r = route;
  r["isolated_world"] = "World_261";
  reject(r);
  r = route;
  r["purpose"] = "startup_observation";
  reject(r);
  r = route;
  r["world_actions"]["entries"][0]["replacement"] = "stone";
  reject(r);
  r = route;
  r["world_actions"]["entries"][0]["expected"] = std::string(129, 'x');
  reject(r);
  r = route;
  r["world_actions"]["entries"].push_back(r["world_actions"]["entries"][0]);
  reject(r);
  r = route;
  r["world_actions"]["entries"] = json::array();
  reject(r);
  r = route;
  r["purpose"] = "startup_observation";
  r["world_actions"]["entries"][0].erase("replacement");
  Expect(!parse(r).at(0).replacement, "read-only cold probe rejected");
  std::cout << "diagnostic world action admission PASS\n";
}
