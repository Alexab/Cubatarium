#include "World.h"
#include "CreatureDefinitionStorage.h"
#include "CreatureVisualFactory.h"
#include "Player.h"
#include "TestMob.h"
#include "User.h"
#include "Camera.h"
#include "ViewEngine.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum {

using json = nlohmann::json;

void World::SetCreatureDefinitionStorage(std::shared_ptr<CreatureDefinitionStorage> storage)
{
 creatureDefinitions_ = std::move(storage);
}

Creature* World::GetCreature(CreatureId id)
{
 const auto it = creatures_.find(id);
 return it != creatures_.end() ? it->second.get() : nullptr;
}

const Creature* World::GetCreature(CreatureId id) const
{
 const auto it = creatures_.find(id);
 return it != creatures_.end() ? it->second.get() : nullptr;
}

Creature* World::GetControlledCreature()
{
 return GetCreature(controlledCreatureId_);
}

const Creature* World::GetControlledCreature() const
{
 return GetCreature(controlledCreatureId_);
}

Creature* World::GetPlayerCreature()
{
 return GetCreature(playerCreatureId_);
}

bool World::SetControlledCreature(CreatureId id)
{
 if (id == 0 || !GetCreature(id)) {
  return false;
 }
 if (controlledCreatureId_ != 0) {
  if (Creature* prev = GetCreature(controlledCreatureId_)) {
   prev->SetPossessed(false);
  }
 }
 controlledCreatureId_ = id;
 if (Creature* c = GetCreature(id)) {
  c->SetPossessed(true);
 }
 return true;
}

CreatureId World::SpawnCreature(const std::string& typeId, const glm::vec3& bodyOrigin)
{
 const CreatureId id = nextCreatureId_++;
 glm::vec3 eyeOffset(0.0f, 1.62f, 0.0f);
 CreatureBoundsProfile boundsProfile;
 CreatureLocomotionCapabilities caps;

 if (creatureDefinitions_) {
  if (const CreatureDefinition* def = creatureDefinitions_->Get(typeId)) {
   eyeOffset = glm::vec3(0.0f, def->eyeHeight, 0.0f);
   boundsProfile = def->bounds;
   caps = def->locomotion;
  }
 }

 std::unique_ptr<Creature> creature;
 if (typeId == "player") {
  auto player = std::make_unique<Player>(id, bodyOrigin);
  player->SetPlayerCharacter(true);
  creature = std::move(player);
 } else if (typeId == "test_mob") {
  creature = std::make_unique<TestMob>(id, bodyOrigin);
 } else {
  creature = std::make_unique<Creature>(id, typeId, bodyOrigin, eyeOffset);
 }

 creature->GetBoundsMutable().profile = boundsProfile;
 if (boundsProfile.restSizeBlocks.x > 0.0f) {
  creature->GetBoundsMutable().currentSizeBlocks = boundsProfile.restSizeBlocks;
 }
 creature->SetCapabilities(caps);
 if (creatureDefinitions_) {
  if (const CreatureDefinition* def = creatureDefinitions_->Get(typeId)) {
   creature->SetVisual(CreateCreatureVisual(*def));
  }
 }
 creatures_[id] = std::move(creature);
 return id;
}

void World::RemoveCreature(CreatureId id)
{
 if (controlledCreatureId_ == id) {
  controlledCreatureId_ = playerCreatureId_;
 }
 if (playerCreatureId_ == id) {
  playerCreatureId_ = 0;
 }
 creatures_.erase(id);
}

void World::ForEachCreature(const std::function<void(Creature&)>& fn)
{
 for (auto& entry : creatures_) {
  if (entry.second) {
   fn(*entry.second);
  }
 }
}

void World::ForEachCreature(const std::function<void(const Creature&)>& fn) const
{
 for (const auto& entry : creatures_) {
  if (entry.second) {
   fn(*entry.second);
  }
 }
}

std::string World::ResolveAnimationTypeId(const Creature& creature) const
{
 if (creature.IsPlayerCharacter()) {
  if (auto user = GetCurrentUser()) {
   const std::string& appearance = user->GetSelectedAppearanceTypeId();
   if (!appearance.empty()) {
    return appearance;
   }
  }
 }
 return creature.GetTypeId();
}

const CreatureDefinition* World::GetCreatureDefinition(const std::string& typeId) const
{
 if (!creatureDefinitions_) {
  return nullptr;
 }
 return creatureDefinitions_->Get(typeId);
}

void World::LinkUsersToPlayerCreatures()
{
 for (auto& entry : Users) {
  if (entry.second && entry.second->GetPlayerCreatureId() == 0 && playerCreatureId_ != 0) {
   entry.second->SetPlayerCreatureId(playerCreatureId_);
  }
 }
}

void World::LoadCreatures(const std::string& file_name)
{
 if (!std::filesystem::exists(file_name)) {
  return;
 }
 try {
  std::ifstream file(file_name);
  json data;
  file >> data;
  if (!data.contains("creatures") || !data["creatures"].is_array()) {
   return;
  }
  for (const auto& c : data["creatures"]) {
   const CreatureId id = c.value("id", 0);
   if (id == 0) {
    continue;
   }
   const std::string type = c.value("type", "test_mob");
   if (type == "player") {
    continue;
   }
   glm::vec3 bodyOrigin(0.0f);
   if (c.contains("body_origin") && c["body_origin"].is_array() && c["body_origin"].size() == 3) {
    bodyOrigin = glm::vec3(c["body_origin"][0].get<float>(), c["body_origin"][1].get<float>(),
                           c["body_origin"][2].get<float>());
   }
   glm::vec3 eyeOffset(0.0f, 1.45f, 0.0f);
   if (creatureDefinitions_) {
    if (const CreatureDefinition* def = creatureDefinitions_->Get(type)) {
     eyeOffset = glm::vec3(0.0f, def->eyeHeight, 0.0f);
    }
   }
   std::unique_ptr<Creature> creature;
   if (type == "player") {
    creature = std::make_unique<Player>(id, bodyOrigin);
   } else if (type == "test_mob") {
    creature = std::make_unique<TestMob>(id, bodyOrigin);
   } else {
    creature = std::make_unique<Creature>(id, type, bodyOrigin, eyeOffset);
   }
   if (creatureDefinitions_) {
    if (const CreatureDefinition* def = creatureDefinitions_->Get(type)) {
     creature->GetBoundsMutable().profile = def->bounds;
     creature->GetBoundsMutable().currentSizeBlocks = def->bounds.restSizeBlocks;
     creature->SetCapabilities(def->locomotion);
     creature->SetVisual(CreateCreatureVisual(*def));
    }
   }
   creature->SetOrientation(c.value("yaw", -90.0f), c.value("pitch", 0.0f));
   if (c.value("movement_mode", "walking") == "flying") {
    creature->GetLocomotion().SetMode(CreatureMovementMode::Flying);
   }
   creature->GetInventory().DeserializeFromJson(c);
   creatures_[id] = std::move(creature);
   nextCreatureId_ = std::max(nextCreatureId_, id + 1);
   if (type == "player") {
    playerCreatureId_ = id;
   }
  }
  if (playerCreatureId_ != 0 && controlledCreatureId_ == 0) {
   controlledCreatureId_ = playerCreatureId_;
  }
 } catch (const std::exception& e) {
  std::cerr << "LoadCreatures: " << e.what() << std::endl;
 }
}

void World::SaveCreatures(const std::string& file_name)
{
 json root;
 root["format_version"] = 1;
 json arr = json::array();
 for (const auto& entry : creatures_) {
  if (!entry.second || entry.second->IsPlayerCharacter()) {
   continue;
  }
  const Creature& c = *entry.second;
  json item;
  item["id"] = c.GetId();
  item["type"] = c.GetTypeId();
  const glm::vec3 body = c.GetBodyOrigin();
  item["body_origin"] = json::array({body.x, body.y, body.z});
  item["yaw"] = c.GetYaw();
  item["pitch"] = c.GetPitch();
  item["movement_mode"] =
      c.GetMovementMode() == CreatureMovementMode::Flying ? "flying" : "walking";
  c.GetInventory().SerializeToJson(item);
  arr.push_back(item);
 }
 root["creatures"] = arr;
 std::ofstream file(file_name);
 file << root.dump(2);
}

} // namespace cutum
