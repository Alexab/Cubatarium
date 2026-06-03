#include "World.h"
#include "CreatureAppearance.h"
#include "CreatureDefinitionStorage.h"
#include "SkinDefinitionStorage.h"
#include "CreatureVisualFactory.h"
#include "Player.h"
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

void World::SetSkinDefinitionStorage(std::shared_ptr<SkinDefinitionStorage> storage)
{
 skinDefinitions_ = std::move(storage);
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

CreatureId World::SpawnCreature(const std::string& speciesId, const glm::vec3& bodyOrigin,
                                const std::string& skinId)
{
 const CreatureDefinition* def =
     creatureDefinitions_ ? creatureDefinitions_->Get(speciesId) : nullptr;
 if (!def) {
  std::cerr << "SpawnCreature: unknown species '" << speciesId << "'" << std::endl;
  return 0;
 }

 const CreatureId id = nextCreatureId_++;
 const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);

 std::unique_ptr<Creature> creature;
 if (def->role == CreatureRole::ControlledDefault) {
  creature = std::make_unique<Player>(id, speciesId, bodyOrigin);
 } else {
  creature = std::make_unique<Creature>(id, speciesId, bodyOrigin, eyeOffset);
 }

 creature->GetBoundsMutable().profile = def->bounds;
 if (def->bounds.restSizeBlocks.x > 0.0f) {
  creature->GetBoundsMutable().currentSizeBlocks = def->bounds.restSizeBlocks;
 }
 creature->SetCapabilities(def->locomotion);
 if (!skinId.empty()) {
  creature->SetSkinId(skinId);
 }
 creature->SetVisual(CreateCreatureVisual(*def));
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

ResolvedCreatureAppearance World::GetResolvedAppearance(const Creature& creature) const
{
 if (!creatureDefinitions_) {
  return ResolvedCreatureAppearance{};
 }
 SkinDefinitionStorage empty;
 const SkinDefinitionStorage& skins = skinDefinitions_ ? *skinDefinitions_ : empty;
 return ResolveCreatureAppearance(*creatureDefinitions_, skins, creature.GetTypeId(),
                                  creature.GetSkinId());
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
   std::string type = c.value("type", "scout");
   if (type == "test_mob") {
    type = "scout";
   }
   if (type == "player") {
    continue;
   }
   const std::string skin = c.value("skin_id", "");
   glm::vec3 bodyOrigin(0.0f);
   if (c.contains("body_origin") && c["body_origin"].is_array() && c["body_origin"].size() == 3) {
    bodyOrigin = glm::vec3(c["body_origin"][0].get<float>(), c["body_origin"][1].get<float>(),
                           c["body_origin"][2].get<float>());
   }
   const CreatureDefinition* def = creatureDefinitions_->Get(type);
   if (!def) {
    continue;
   }
   const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);
   auto creature = std::make_unique<Creature>(id, type, bodyOrigin, eyeOffset);
   creature->GetBoundsMutable().profile = def->bounds;
   creature->GetBoundsMutable().currentSizeBlocks = def->bounds.restSizeBlocks;
   creature->SetCapabilities(def->locomotion);
   if (!skin.empty()) {
    creature->SetSkinId(skin);
   }
   creature->SetVisual(CreateCreatureVisual(*def));
   creature->SetOrientation(c.value("yaw", -90.0f), c.value("pitch", 0.0f));
   if (c.value("movement_mode", "walking") == "flying") {
    creature->GetLocomotion().SetMode(CreatureMovementMode::Flying);
   }
   creature->GetInventory().DeserializeFromJson(c);
   creatures_[id] = std::move(creature);
   nextCreatureId_ = std::max(nextCreatureId_, id + 1);
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
  if (!c.GetSkinId().empty()) {
   item["skin_id"] = c.GetSkinId();
  }
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
