#include "BlockDefinitionStorage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum {

namespace fs = std::filesystem;
using json = nlohmann::json;

void BlockDefinitionStorage::Load(const std::string& modelsPath)
{
 byId_.clear();
 nameToId_.clear();
 try {
  for (const auto& entry : fs::directory_iterator(modelsPath)) {
   if (entry.path().extension() != ".json") {
    continue;
   }
   std::ifstream file(entry.path());
   if (!file.is_open()) {
    continue;
   }
   std::stringstream buffer;
   buffer << file.rdbuf();
   json d;
   try {
    d = json::parse(buffer.str());
   } catch (const json::exception& e) {
    std::cerr << "BlockDefinitionStorage: parse error " << entry.path() << ": " << e.what()
              << std::endl;
    continue;
   }
   BlockDefinition def;
   def.name = d.value("name", "");
   def.id = static_cast<BlockId>(d.value("id", 0));
   if (def.name.empty() || def.id == BLOCK_AIR) {
    continue;
   }
   if (d.contains("animation")) {
    def.animation = ParseAnimationFromJson(d["animation"]);
   }
   if (d.contains("physics")) {
    def.physics = ParsePhysicsFromJson(d["physics"]);
   } else {
    def.physics = BlockPhysicsProfile::Solid();
   }
   if (d.contains("render")) {
    def.render = ParseRenderFromJson(d["render"]);
   }
   if (d.contains("physics") && d["physics"].is_object() && d["physics"].contains("preset")
       && d["physics"]["preset"].is_string()) {
    ApplyRenderPresetDefaults(def.render, d["physics"]["preset"].get<std::string>());
   }
   byId_[def.id] = def;
   nameToId_[def.name] = def.id;
  }
 } catch (const fs::filesystem_error& ex) {
  std::cerr << "BlockDefinitionStorage::Load: " << ex.what() << std::endl;
 }
}

const BlockDefinition* BlockDefinitionStorage::GetById(BlockId id) const
{
 const auto it = byId_.find(id);
 if (it != byId_.end()) {
  return &it->second;
 }
 return nullptr;
}

const BlockDefinition* BlockDefinitionStorage::GetByName(const std::string& name) const
{
 const auto it = nameToId_.find(name);
 if (it == nameToId_.end()) {
  return nullptr;
 }
 return GetById(it->second);
}

} // namespace cutum
