#pragma once
#include "App/Platform/CreatureQaSaveProbe.h"
#include "Core/Progress/IUProgressSink.h"
#include "Creatures/Player/User.h"
#include "Render/Camera/Camera.h"
#include <chrono>

namespace cutum
{
// Called once by flight QA after the normal cooperative world load. The marker
// and disposable-world prefix are mandatory before any user/control/save action.
inline void RunCreatureQaWorldUserProbe(UCore &core,UWorld &world)
{
  const char *output=std::getenv("CUBA_FLIGHT_QA_WORLD_USER");
  if(!output || !*output)return;
  using namespace creature_save_qa;
  namespace fs=std::filesystem;
  const auto worldFolder=fs::absolute(world.GetWorldFolderPath()).lexically_normal();
  const std::string name=worldFolder.filename().string();
  if(name.rfind("CreatureQaUser_",0)!=0 || world.GetWorldName()!=name)
    throw std::runtime_error("User save QA requires a named disposable CreatureQaUser_ world");
  const auto marker=Read(worldFolder/"creature_user_qa_fixture.json");
  if(marker.value("schema","")!="creature-user-world-fixture.v1" || marker.value("world","")!=name)
    throw std::runtime_error("User save QA fixture marker mismatch");
  const auto folder=fs::absolute(output).lexically_normal();
  fs::create_directories(folder);
  const auto user=world.GetCurrentUser();auto *player=world.GetPlayerCreature();
  const auto camera=world.GetCurrentUserCamera();
  if(!user || !player || !camera)throw std::runtime_error("User save QA actual loaded user/player/camera missing");
  const auto capture=[&]() {
    Json actors=Json::object();world.ForEachCreature([&](const UCreature &c){actors[std::to_string(c.GetId())]=State(c);});
    const auto appearance=world.GetResolvedAppearanceShared(*player);
    const auto position=camera->GetPosition();
    return Json{{"selected_skin",user->GetSelectedSkinId()},
      {"selected_legacy_appearance",user->GetSelectedAppearanceTypeId()},
      {"user_player_id",user->GetPlayerCreatureId()},{"player_id",player->GetId()},
      {"controlled_id",world.GetControlledCreatureId()},{"is_player_character",player->IsPlayerCharacter()},
      {"player_possessed",player->IsPossessed()},{"player_raw_skin",player->GetSkinId()},
      {"player_effective_skin",appearance->effectiveSkinId},{"player_state",State(*player)},
      {"actors",actors},{"camera_position",{position.x,position.y,position.z}},
      {"camera_yaw",camera->GetYaw()},{"camera_pitch",camera->GetPitch()},
      {"first_person",camera->GetPerspective()==CameraPerspective::FirstPerson},
      {"fp_view_enabled",ShouldDrawFpViewmodel(world.GetViewSettings())}};
  };
  Json report={{"schema","creature-user-world-native.v1"},{"world",name},
    {"scope","Normal cooperative load and save, actual current user/appearance/inventory, sampled control roundtrip; no UI pack/terrain/FP animation/performance acceptance"},
    {"loaded",capture()},{"control_transitions",Json::array()}};
  bool passed=player->IsPlayerCharacter() && player->GetId()==user->GetPlayerCreatureId() &&
    world.GetControlledCreatureId()==player->GetId() &&
    user->GetSelectedSkinId()==marker.at("expected_skin").get<std::string>() &&
    world.GetResolvedAppearanceShared(*player)->effectiveSkinId==user->GetSelectedSkinId();
  const auto originalId=world.GetControlledCreatureId();
  for(const auto *species:{"cow","badger","rigid_demo_walker","fire_spirit","skeleton"})
  {
    UCreature *target=nullptr;world.ForEachCreature([&](UCreature &c){if(!target && c.GetTypeId()==species && !c.IsPlayerCharacter())target=&c;});
    if(!target)throw std::runtime_error("User save QA control representative missing");
    const auto targetState=State(*target);
    const bool controlled=world.SetControlledCreature(target->GetId());
    const auto appearance=world.GetResolvedAppearanceShared(*target);
    const auto *definition=world.GetCreatureDefinition(target->GetTypeId());
    const bool transfer=controlled && world.GetControlledCreature()==target && target->IsPossessed() && !player->IsPossessed();
    const bool returned=world.SetControlledCreature(originalId) && world.GetControlledCreature()==player &&
      player->IsPossessed() && !target->IsPossessed() && State(*target)==targetState;
    passed=passed && transfer && returned;
    report["control_transitions"].push_back({{"species",species},{"id",target->GetId()},
      {"runtime_backend",CreatureUsesBillboardSprite(*definition)?"sprite":definition->visual.backend},
      {"effective_skin",appearance->effectiveSkinId},{"transfer",transfer},{"return",returned}});
  }
  report["before_save"]=capture();
  passed=passed && report["loaded"]==report["before_save"];
  struct Progress final: IUProgressSink
  {
    Json phases=Json::array();
    void Begin(WorldOperationKind) override {}
    void Report(const std::string &phase,float,const std::string &,int,int) override
    {if(phases.empty() || phases.back()!=phase)phases.push_back(phase);}
    void End(bool,const std::string &) override {}
  } progress;
  const auto started=std::chrono::steady_clock::now();
  world.BeginCooperativeSave(worldFolder.string());
  size_t ticks=0;
  while(!world.TickCooperativeSave(progress,64))
  {
    if(++ticks>100000 || std::chrono::steady_clock::now()-started>std::chrono::seconds(120))
      throw std::runtime_error("User QA cooperative save exceeded bounded observation time");
  }
  report["save_ticks"]=ticks;report["save_phases"]=progress.phases;
  report["save_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  report["after_save"]=capture();
  // SaveUsers intentionally refreshes the user's camera cache. Compare actual
  // entities and selected appearance, not a stale pre-save UUser position cache.
  passed=passed && report["before_save"]==report["after_save"] && !world.HasActiveCooperativeOperation();
  const auto savedUsers=Read(worldFolder/"users.json");
  const auto savedCreatures=Read(worldFolder/"creatures.json");
  Write(folder/"saved_users.json",savedUsers);Write(folder/"saved_creatures.json",savedCreatures);
  Write(folder/"saved_world_data.json",Read(worldFolder/"world_data.json"));
  Write(folder/"fixture_marker.json",marker);
  report["saved_user_count"]=savedUsers.size();report["saved_creature_count"]=savedCreatures.at("creatures").size();
  report["saved_current_user"]=savedUsers.at(world.GetCurrentUserName());
  report["passed"]=passed;Write(folder/"manifest.json",report);
  if(!passed)throw std::runtime_error("User save QA state/control/cooperative save failed; inspect manifest");
}
}
