#include "App/Diagnostics/DiagnosticWorldActions.h"
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
