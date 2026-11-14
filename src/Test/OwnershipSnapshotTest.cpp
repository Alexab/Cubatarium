#include "World/Persistence/OwnershipSnapshot.h"
#include "Game/Physics/ThrowSettings.h"
#include <chrono>
#include <iostream>
using namespace cutum;
int main()
{
  int errors = 0;
  auto check = [&](bool ok, const char *name)
  {
    if (!ok)
    {
      std::cerr << name << '\n';
      ++errors;
    }
  };
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("cubatarium-ownership-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  auto write = [&](int count, int drops)
  {
    WriteOwnershipJson(dir / "users.json",
                       {{"player",
                         {{"inventory_version", 4},
                          {"owned_slots", {count}},
                          {"human_appearance", {{"hair", "new"}}}}}});
    WriteOwnershipJson(dir / "creatures.json",
                       {{"creatures", nlohmann::json::array()},
                        {"world_physical_v2", {drops}}});
  };
  try
  {
    WriteOwnershipJson(dir / "users.json", nullptr);
    WriteOwnershipJson(dir / "creatures.json",
                       {{"creatures", nlohmann::json::array()}});
    SaveOwnershipBundle(dir);
    check(LoadOwnershipBundle(dir)["users"].is_object(),
          "empty new world has a valid empty ownership map");
    std::filesystem::remove(dir / "ownership_snapshot.json");
    write(5, 0);
    SaveOwnershipBundle(dir);
    write(2, 3);
    SaveOwnershipBundle(dir);
    check(LoadOwnershipBundle(dir)["generation"] == 2,
          "two generations committed");
    WriteOwnershipJson(dir / "ownership_snapshot.json", {{"complete", false}});
    write(999, 999);
    RestoreOwnershipBundle(dir);
    check(ReadOwnershipJson(dir / "users.json")["player"]["owned_slots"][0] ==
              5,
          "interrupted save restores previous inventory");
    check(ReadOwnershipJson(dir / "creatures.json")["world_physical_v2"][0] ==
              0,
          "restored world bodies match inventory generation");
    WriteOwnershipJson(dir / "ownership_snapshot.json.tmp",
                       {{"complete", true}, {"generation", 999}});
    check(LoadOwnershipBundle(dir)["generation"] == 1,
          "uncommitted temporary file ignored");
    WriteOwnershipJson(dir / "ownership_snapshot.previous.json",
                       {{"complete", true}});
    bool refused = false;
    try
    {
      LoadOwnershipBundle(dir);
    }
    catch (const std::runtime_error &)
    {
      refused = true;
    }
    check(refused, "invalid generations fail closed rather than mix sources");
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    ++errors;
  }
  ThrowSettings rules;
  check(rules.Allows(ThrowCategory::Tool), "free default permits tools");
  rules.preset = ThrowPreset::Restricted;
  check(rules.Allows(ThrowCategory::Block) &&
            rules.Allows(ThrowCategory::Material) &&
            !rules.Allows(ThrowCategory::Food) &&
            !rules.Allows(ThrowCategory::General),
        "restricted categories");
  rules.preset = ThrowPreset::Custom;
  rules.allowed = 1u << static_cast<unsigned>(ThrowCategory::Food);
  nlohmann::json saved = rules;
  auto restored = saved.get<ThrowSettings>();
  check(restored.Allows(ThrowCategory::Food) &&
            !restored.Allows(ThrowCategory::Tool),
        "custom rules roundtrip");
  std::filesystem::remove_all(dir);
  return errors ? 1 : 0;
}
