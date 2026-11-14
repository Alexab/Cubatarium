#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace cutum
{
inline nlohmann::json ReadOwnershipJson(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read ownership snapshot: " +
                             path.string());
  nlohmann::json j;
  in >> j;
  return j;
}
inline void WriteOwnershipJson(const std::filesystem::path &path,
                               const nlohmann::json &j)
{
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error("Cannot open ownership snapshot");
    out << j.dump();
    out.flush();
    if (!out)
      throw std::runtime_error("Cannot write ownership snapshot");
  }
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot commit ownership snapshot");
#else
  std::filesystem::rename(tmp, path);
#endif
}
inline std::string OwnershipChecksum(const nlohmann::json &users,
                                     const nlohmann::json &creatures)
{
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : users.dump() + creatures.dump())
  {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return std::to_string(hash);
}
inline bool ValidOwnershipBundle(const nlohmann::json &j)
{
  return j.is_object() && j.value("complete", false) && j.contains("users") &&
         (j["users"].is_object() || j["users"].is_null()) &&
         j.contains("creatures") && j["creatures"].is_object() &&
         j["creatures"].contains("creatures") &&
         j["creatures"]["creatures"].is_array() &&
         j.value("checksum", std::string{}) ==
             OwnershipChecksum(j["users"], j["creatures"]);
}
inline nlohmann::json LoadOwnershipBundle(const std::filesystem::path &folder)
{
  nlohmann::json best;
  bool found = false;
  for (const char *name :
       {"ownership_snapshot.json", "ownership_snapshot.previous.json"})
  {
    const auto path = folder / name;
    if (!std::filesystem::exists(path))
      continue;
    found = true;
    try
    {
      auto j = ReadOwnershipJson(path);
      if (ValidOwnershipBundle(j) &&
          (best.is_null() || j.value("generation", uint64_t{0}) >
                                 best.value("generation", uint64_t{0})))
        best = std::move(j);
    }
    catch (const nlohmann::json::exception &)
    {
    }
    catch (const std::runtime_error &)
    {
    }
  }
  if (found && best.is_null())
    throw std::runtime_error(
        "Both ownership snapshot generations are invalid; recovery required");
  return best;
}
inline void SaveOwnershipBundle(const std::filesystem::path &folder)
{
  const auto previous = LoadOwnershipBundle(folder);
  auto users = ReadOwnershipJson(folder / "users.json");
  if (users.is_null())
    users = nlohmann::json::object();
  if (!users.is_object())
    throw std::runtime_error("Ownership users must be an object");
  auto creatures = ReadOwnershipJson(folder / "creatures.json");
  if (!creatures.is_object() || !creatures.contains("creatures") ||
      !creatures["creatures"].is_array())
    throw std::runtime_error("Invalid ownership creature snapshot");
  const uint64_t generation =
      previous.is_null() ? 1 : previous.value("generation", uint64_t{0}) + 1;
  nlohmann::json bundle = {{"version", 1},
                           {"generation", generation},
                           {"users", users},
                           {"creatures", creatures},
                           {"checksum", OwnershipChecksum(users, creatures)},
                           {"complete", true}};
  if (!previous.is_null())
    WriteOwnershipJson(folder / "ownership_snapshot.previous.json", previous);
  WriteOwnershipJson(folder / "ownership_snapshot.json", bundle);
}
inline void RestoreOwnershipBundle(const std::filesystem::path &folder)
{
  const auto bundle = LoadOwnershipBundle(folder);
  if (bundle.is_null())
    return;
  nlohmann::json users;
  try
  {
    users = ReadOwnershipJson(folder / "users.json");
  }
  catch (const std::exception &)
  {
    users = bundle["users"];
  }
  if (!users.is_object())
    users = bundle["users"].is_object() ? bundle["users"]
                                        : nlohmann::json::object();
  const char *keys[] = {"inventory_version", "inventory_creative",
                        "next_instance_id",  "active_bar",
                        "active_slot",       "owned_slots",
                        "creative_slots",    "equipped_armor",
                        "creative_armor",    "equipped_offhand",
                        "creative_offhand",  "inventory_recovery",
                        "creative_recovery", "inventory_migration_warnings",
                        "player_creature_id"};
  for (auto it = bundle["users"].begin(); it != bundle["users"].end(); ++it)
  {
    if (!users.contains(it.key()))
      users[it.key()] = it.value();
    else
      for (const auto *key : keys)
        if (it.value().contains(key))
          users[it.key()][key] = it.value()[key];
  }
  WriteOwnershipJson(folder / "users.json", users);
  WriteOwnershipJson(folder / "creatures.json", bundle["creatures"]);
}
} // namespace cutum
