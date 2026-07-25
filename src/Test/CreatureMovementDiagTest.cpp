#include "World/Diagnostics/CreatureMovementDiagnostics.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{
// Stub for unit test (diagnostics otherwise resolve via App/Core).
std::filesystem::path GetExecutableDirectory()
{
  return std::filesystem::temp_directory_path();
}
} // namespace cutum

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_movement_diag_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::UCreatureMovementDiagnostics;
  using cutum::CreatureMovementDiagRecord;
  using json = nlohmann::json;

  const auto log_path =
      std::filesystem::temp_directory_path() / "creature_movement_diag_test.jsonl";
  std::error_code ec;
  std::filesystem::remove(log_path, ec);

  UCreatureMovementDiagnostics::SetLogPathOverride(log_path);
  UCreatureMovementDiagnostics::ClearLog();
  UCreatureMovementDiagnostics::SetEnabled(false);
  UCreatureMovementDiagnostics::SetVerbose(false);
  UCreatureMovementDiagnostics::SetFocusId(0);

  CreatureMovementDiagRecord sample;
  sample.event = "intent";
  sample.creatureId = 42;
  sample.typeId = "pig";
  sample.habitat = "terrestrial";
  sample.behavior = "wander";
  sample.body = {1.0f, 64.0f, 2.0f};
  sample.intentDir = {1.0f, 0.0f, 0.0f};
  sample.intentSpeed = 3.0f;
  sample.activityTick = true;

  Expect(UCreatureMovementDiagnostics::Record(sample) < 0,
         "disabled should not record");

  UCreatureMovementDiagnostics::SetEnabled(true);
  Expect(UCreatureMovementDiagnostics::Record(sample) < 0,
         "intent without focus should not record by default");

  UCreatureMovementDiagnostics::SetFocusId(99);
  Expect(UCreatureMovementDiagnostics::Record(sample) < 0,
         "focus mismatch should not record");

  UCreatureMovementDiagnostics::SetFocusId(42);
  const int index = UCreatureMovementDiagnostics::Record(sample);
  Expect(index == 1, "first sample index should be 1");
  Expect(UCreatureMovementDiagnostics::GetSampleCount() == 1,
         "sample count should be 1");
  Expect(UCreatureMovementDiagnostics::GetRingSize() == 1, "ring size should be 1");

  UCreatureMovementDiagnostics::Flush();
  std::ifstream in(log_path);
  Expect(in.is_open(), "log file should exist");
  std::string line;
  Expect(static_cast<bool>(std::getline(in, line)), "log should have a line");
  const json parsed = json::parse(line);
  Expect(parsed.value("schema", "") == "creature_movement_diag.v1",
         "schema key");
  Expect(parsed.value("event", "") == "intent", "event key");
  Expect(parsed.value("creatureId", 0ull) == 42ull, "creatureId key");
  Expect(parsed.contains("body"), "body key");
  Expect(parsed.contains("intentDir"), "intentDir key");
  Expect(parsed.contains("travel"), "travel key");
  Expect(parsed.value("activityTick", false), "activityTick key");
  in.close();

  // Important events record without focus and flush immediately.
  UCreatureMovementDiagnostics::SetFocusId(0);
  CreatureMovementDiagRecord fail;
  fail.event = "path_fail";
  fail.creatureId = 7;
  fail.reason = "no_path";
  Expect(UCreatureMovementDiagnostics::Record(fail) > 0,
         "path_fail should record without focus");

  const auto dump_path =
      std::filesystem::temp_directory_path() / "creature_movement_diag_test.dump.jsonl";
  Expect(UCreatureMovementDiagnostics::DumpRing(dump_path), "dump should succeed");
  Expect(UCreatureMovementDiagnostics::ClearLog(), "clear should succeed");
  Expect(UCreatureMovementDiagnostics::GetSampleCount() == 0,
         "sample count cleared");
  Expect(!std::filesystem::exists(log_path), "log file removed");

  UCreatureMovementDiagnostics::SetEnabled(false);
  UCreatureMovementDiagnostics::SetVerbose(false);
  UCreatureMovementDiagnostics::ClearLogPathOverride();
  std::filesystem::remove(dump_path, ec);

  std::cout << "creature_movement_diag_test: OK" << std::endl;
  return 0;
}
