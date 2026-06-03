#include "CreatureDefinitionStorage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum {

namespace {

glm::vec3 ReadVec3(const nlohmann::json& arr, const glm::vec3& fallback)
{
 if (!arr.is_array() || arr.size() < 3) {
  return fallback;
 }
 return glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
}

} // namespace

void CreatureDefinitionStorage::Load(const std::string& folder)
{
 definitions_.clear();
 if (!std::filesystem::exists(folder)) {
  return;
 }
 for (const auto& entry : std::filesystem::directory_iterator(folder)) {
  if (entry.path().extension() == ".json") {
   LoadFile(entry.path().string());
  }
 }
 std::cout << "CreatureDefinitionStorage: loaded " << definitions_.size() << " definitions" << std::endl;
}

bool CreatureDefinitionStorage::LoadFile(const std::string& path)
{
 try {
  std::ifstream file(path);
  if (!file.is_open()) {
   return false;
  }
  nlohmann::json data;
  file >> data;
  CreatureDefinition def;
  def.id = data.value("id", "");
  if (def.id.empty()) {
   return false;
  }
  if (data.contains("bounds")) {
   const auto& b = data["bounds"];
   def.bounds.restSizeBlocks = ReadVec3(b.value("rest", nlohmann::json::array()), def.bounds.restSizeBlocks);
   def.bounds.maxSizeBlocks = ReadVec3(b.value("max", nlohmann::json::array()), def.bounds.maxSizeBlocks);
   def.bounds.minSizeBlocks = ReadVec3(b.value("min", nlohmann::json::array()), def.bounds.minSizeBlocks);
  }
  def.eyeHeight = data.value("eye_height", def.eyeHeight);
  if (data.contains("locomotion")) {
   const auto& loc = data["locomotion"];
   def.locomotion.canFly = loc.value("can_fly", true);
   def.locomotion.canCrouch = loc.value("can_crouch", true);
   def.locomotion.canJump = loc.value("can_jump", true);
  }
  if (data.contains("visual") && data["visual"].is_object()) {
   def.visualBackend = data["visual"].value("backend", def.visualBackend);
  }
  definitions_[def.id] = def;
  return true;
 } catch (const std::exception& e) {
  std::cerr << "CreatureDefinitionStorage: " << path << ": " << e.what() << std::endl;
  return false;
 }
}

const CreatureDefinition* CreatureDefinitionStorage::Get(const std::string& id) const
{
 const auto it = definitions_.find(id);
 if (it == definitions_.end()) {
  return nullptr;
 }
 return &it->second;
}

} // namespace cutum
