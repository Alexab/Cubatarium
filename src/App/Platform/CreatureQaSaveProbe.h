#pragma once
#include "App/Platform/CreatureQaResourceCacheProbe.h"
#include "Creatures/Core/UCreatureFactory.h"
#include "Creatures/Core/CreaturePersistenceJson.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Game/ModePolicy.h"
#include "Items/ItemDefinitionStorage.h"
#include "World/Environment/WorldEnvironment.h"
#include "World/Core/World.h"

namespace cutum
{
namespace creature_save_qa
{
using Json=nlohmann::json;
inline Json State(const UCreature &c)
{
  Json out={{"id",c.GetId()},{"type",c.GetTypeId()},{"skin_id",c.GetSkinId()},
    {"spawn_source",CreatureSpawnSourceToString(c.GetSpawnSource())},{"yaw",c.GetYaw()},
    {"pitch",c.GetPitch()},{"footprint_swapped",c.IsFootprintSwapped()},
    {"movement_mode",c.GetMovementMode()==CreatureMovementMode::Flying?"flying":"walking"}};
  if(c.GetTypeId()=="human")out["human_appearance"]=c.GetHumanAppearance();
  WriteCreatureBodyOrigin(out,c.GetBodyOrigin());
  c.GetInventory().SerializeToJson(out);CreatureStatsJson::Write(out,c.GetVitals(),c.GetAttributes());
  return out;
}
inline Json Draw(UCreature &c,UWorldEnvironment &environment,UCore &core)
{
  const auto *def=environment.GetCreatureDefinition(c.GetTypeId());
  auto *visual=c.GetVisual();if(!def || !visual)throw std::runtime_error("Save QA missing actual visual");
  const auto appearance=environment.GetResolvedAppearanceShared(c);
  visual->SetAppearanceShared(appearance);
  CreatureLocomotionFacts facts;facts.state=LocomotionState::Walk;
  facts.horizontalSpeed=def->locomotion.walkSpeed;facts.animPhase=.375f;
  visual->UpdatePose(c,facts,CreaturePoseParams{},*def,.1375f);
  RenderSettings settings;settings.CreatureTexturedParts=true;settings.CreatureWireframeOverlay=false;
  CreatureDrawQueue queue;CreatureDrawContext context;context.Queue=&queue;
  context.Textures=core.GetCreatureTextureStorage().get();context.Settings=&settings;
  context.Items=core.GetItemDefinitionStorage().get();context.CameraPosition=c.GetBodyOrigin()+glm::vec3(3.f,2.f,4.f);
  visual->SubmitDraw(context,glm::mat4(1.f));CreatureQaResourceDrawSnapshot snapshot;queue.Flush(snapshot);
  const auto size=c.GetCollisionSize();const auto eye=c.GetEyeOffset();
  return {{"hash",snapshot.hash},{"draws",snapshot.draws},{"textures",snapshot.textures},
    {"geometry_hashes",snapshot.geometryHashes},{"pose_values",snapshot.poseValues},
    {"effective_skin",appearance->effectiveSkinId},{"backend",CreatureUsesBillboardSprite(*def)?"sprite":def->visual.backend},
    {"collision_size",{size.x,size.y,size.z}},{"eye_offset",{eye.x,eye.y,eye.z}}};
}
inline void Write(const std::filesystem::path &path,const Json &data)
{
  std::ofstream file(path);file<<data.dump(2)<<'\n';if(!file)throw std::runtime_error("Save QA write failed: "+path.string());
}
inline Json Read(const std::filesystem::path &path)
{
  std::ifstream file(path);Json data;file>>data;return data;
}
}

// Explicit native QA only. These environments own their entities/files, never
// tick, and only query the loaded world for the normal loader's collision checks.
inline void ExportCreatureQaSaveProbe(UCore &core,UWorld &world,const std::filesystem::path &directory)
{
  const char *enabled=std::getenv("CUBA_FLIGHT_QA_SAVE_PROBE");
  if(!enabled || std::string(enabled)!="1")return;
  using namespace creature_save_qa;
  const auto defs=world.GetCreatureDefinitionStorage();const auto skins=world.GetSkinDefinitionStorage();
  if(!defs || !skins || defs->ListAllIds().size()!=56 || skins->Count()!=5)throw std::runtime_error("Save QA requires production56+5");
  const auto folder=directory/"save";std::filesystem::create_directories(folder);
  const auto *controlled=world.GetControlledCreature();const Json controlledBefore=controlled?State(*controlled):Json();
  Json worldBefore=Json::object();world.ForEachCreature([&](const UCreature &c){worldBefore[std::to_string(c.GetId())]=State(c);});
  struct Resource{std::string label,species,skin,legacyType,legacySkin;};
  std::vector<Resource> resources;
  auto ids=defs->ListAllIds();std::sort(ids.begin(),ids.end());
  for(const auto &id:ids)resources.push_back({id,id,"","",""});
  auto skinIds=skins->ListEquippable();std::sort(skinIds.begin(),skinIds.end());
  for(const auto &id:skinIds)resources.push_back({id,skins->Get(id)->creatureId,id,"",""});
  resources.push_back({"legacy_scout","sheep","sheep_wool_golden","scout","scout_golden"});
  resources.push_back({"legacy_drifter","wolf","wolf_snow","drifter","drifter_ice"});
  resources.push_back({"legacy_brute","sand_monster","","brute","brute_rust"});
  Json report={{"schema","creature-save-roundtrip.v1"},
    {"scope","Real isolated entity LoadCreatures/SaveCreatures; synthetic supported legacy/v2 inputs; not users, whole-world cooperative saves, terrain recovery or art acceptance"},
    {"cases",Json::array()}};bool passed=true;
  for(const bool legacy:{true,false})
  {
    const std::string prefix=legacy?"legacy":"v2";
    UWorldEnvironment first(world),second(world);
    for(auto *env:{&first,&second}){env->SetCreatureDefinitionStorage(defs);env->SetSkinDefinitionStorage(skins);}
    Json input={{"creatures",Json::array()}},expected=Json::object(),expectedDraw=Json::object();
    if(!legacy)input["format_version"]=2;
    size_t index=0;
    for(const auto &r:resources)
    {
      const auto *def=defs->Get(r.species);const CreatureId id=100000+static_cast<CreatureId>(index);
      Json saved={{"yaw",index%2?90.f:0.f},{"pitch",index%3?12.f:-8.f},
        {"movement_mode",index%3==0?"flying":"walking"}};
      CreatureBuildParams params;params.Id=id;params.bodyOrigin={10000.f+static_cast<float>(index)*32.f,512.f,10000.f};
      params.SkinId=r.skin;params.AllowPlayerRole=false;params.SavedState=&saved;
      params.AllowFlight=ModePolicy::AllowsFlight(world.GetGameMode(),def->habitat);
      params.Source=legacy?CreatureSpawnSource::LegacySave:CreatureSpawnSource::Qa;
      auto creature=UCreatureFactory::Build(*def,params);creature->UpdateFootprintOrientation(world);
      if(!legacy && r.species=="human") {HumanAppearance look;look.bodyVariant="female";look.hairstyleId="long";look.hairColorId="ginger";creature->SetHumanAppearance(look);}
      auto &v=creature->GetVitals();v.health=v.maxHealth*.75f;v.satiety=v.maxSatiety*.625f;
      v.thirst=v.maxThirst*.5f;v.fatigue=v.maxFatigue*.25f;v.breath=v.maxBreath*.875f;
      auto &inv=creature->GetInventory();inv.AddItem("wood_sword",3);inv.AddItem("wood_torch",7);
      inv.EnsureHotbarCount(2);
      InventoryEntryRef main;main.kind=InventoryEntryKind::Item;main.Id="wood_sword";main.count=1;main.empty=false;main.wear=.375f;
      inv.AssignToHotbar(1,4,main);inv.SetActiveSlot(1,4);
      if(r.species=="human" || r.species=="bot" || r.species=="zombie" || r.species=="skeleton")
      {
        const char *slots[]={"head","chest","legs","feet"};
        for(size_t slot=0;slot<4;++slot)
        {auto armor=main;armor.Id=std::string("copper_")+slots[slot];armor.wear=.125f*static_cast<float>(slot+1);
          if(!inv.EquipArmor(slot,armor,*core.GetItemDefinitionStorage()))throw std::runtime_error("Save QA armor equip failed");}
        auto off=main;off.Id="wood_torch";off.wear=.25f;
        if(!inv.EquipOffhand(off,*core.GetItemDefinitionStorage()))throw std::runtime_error("Save QA offhand equip failed");
      }
      const std::string key=std::to_string(id);auto state=State(*creature);
      expected[key]=state;expectedDraw[key]=Draw(*creature,first,core);
      if(!r.legacyType.empty()){state["type"]=r.legacyType;state["skin_id"]=r.legacySkin;}
      if(legacy)state.erase("spawn_source");
      input["creatures"].push_back(state);++index;
    }
    Write(folder/(prefix+"_input.json"),input);
    Write(folder/(prefix+"_expected.json"),expected);
    first.LoadCreatures((folder/(prefix+"_input.json")).string());
    first.SaveCreatures((folder/(prefix+"_saved_once.json")).string());
    second.LoadCreatures((folder/(prefix+"_saved_once.json")).string());
    second.SaveCreatures((folder/(prefix+"_saved_twice.json")).string());
    // Repeat into an existing path exercises Windows' replace-save path too.
    second.SaveCreatures((folder/(prefix+"_saved_twice.json")).string());
    size_t firstCount=0,secondCount=0;
    first.ForEachCreature([&](const UCreature &){++firstCount;});second.ForEachCreature([&](const UCreature &){++secondCount;});
    Json observations=Json::array();bool valid=firstCount==resources.size() && secondCount==resources.size();
    for(size_t i=0;i<resources.size();++i)
    {
      const CreatureId id=100000+static_cast<CreatureId>(i);const auto key=std::to_string(id);
      auto *a=first.GetCreature(id);auto *b=second.GetCreature(id);
      Json row={{"id",id},{"resource",resources[i].label},{"expected_state",expected[key]},{"expected_draw",expectedDraw[key]}};
      bool ok=a && b;
      if(ok)
      {
        row["loaded_state"]=State(*a);row["reloaded_state"]=State(*b);
        row["loaded_draw"]=Draw(*a,first,core);row["reloaded_draw"]=Draw(*b,second,core);
        ok=row["loaded_state"]==expected[key] && row["reloaded_state"]==expected[key] &&
          row["loaded_draw"]==expectedDraw[key] && row["reloaded_draw"]==expectedDraw[key] && row["loaded_draw"]["draws"].get<size_t>()>0;
      }
      row["passed"]=ok;valid=valid && ok;observations.push_back(row);
    }
    const auto once=Read(folder/(prefix+"_saved_once.json")),twice=Read(folder/(prefix+"_saved_twice.json"));
    const auto canonical=[](const Json &root){Json map=Json::object();for(const auto &c:root.at("creatures"))map[std::to_string(c.at("id").get<CreatureId>())]=c;return map;};
    valid=valid && once.at("format_version")==2 && twice.at("format_version")==2 && canonical(once)==canonical(twice);
    report["cases"].push_back({{"kind",prefix},{"first_count",firstCount},{"second_count",secondCount},
      {"canonical_save_stable",canonical(once)==canonical(twice)},{"resources",observations},{"passed",valid}});
    passed=passed && valid;
  }
  const auto *after=world.GetControlledCreature();
  report["controlled_actor_unchanged"]=(after?State(*after):Json())==controlledBefore;
  Json worldAfter=Json::object();world.ForEachCreature([&](const UCreature &c){worldAfter[std::to_string(c.GetId())]=State(c);});
  report["world_actors_unchanged"]=worldBefore==worldAfter;report["world_actor_count"]=worldBefore.size();
  report["passed"]=passed && report["controlled_actor_unchanged"].get<bool>() && report["world_actors_unchanged"].get<bool>();Write(folder/"manifest.json",report);
  if(!report["passed"].get<bool>())throw std::runtime_error("Native creature save roundtrip failed; inspect save/manifest.json");
}
}
