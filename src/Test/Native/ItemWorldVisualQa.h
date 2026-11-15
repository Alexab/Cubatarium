#pragma once
#include "App/Application.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Visual/CarryPresentation.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Items/FpViewmodelRenderer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/GlIncludes.h"
#include "ThirdParty/stb_image_write.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cutum
{
/// Opt-in actual-world presentation capture. The runner restores the dedicated
/// QA world; no synthetic preview or alternate item renderer is involved.
class UItemWorldVisualQa
{
public:
  UItemWorldVisualQa(UWorld &world, const std::filesystem::path &path)
      : Path(path)
  {
    if (world.GetWorldName().size() < 2 ||
        world.GetWorldName().substr(world.GetWorldName().size() - 2) != "QA")
      throw std::runtime_error(
          "World item capture requires a dedicated QA world");
    std::ifstream input(path.parent_path() / "request.json");
    input >> Request;
    Ids = Request.at("ids").get<std::vector<std::string>>();
    Interaction = Request.value("interaction", std::string{});
    if (Ids.empty())
      throw std::runtime_error("World item capture has no IDs");
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    if (!actor || !camera || actor->GetTypeId() != "human")
      throw std::runtime_error("World item capture requires a human");
    auto appearance = actor->GetHumanAppearance();
    appearance.bodyVariant = Request.value("body", "male");
    actor->SetHumanAppearance(appearance);
    std::vector<uint64_t> others;
    world.ForEachCreature(
        [&](const UCreature &c)
        {
          if (c.GetId() != actor->GetId())
            others.push_back(c.GetId());
        });
    for (auto id : others)
      world.RemoveCreature(id);
    Base = WorldPosToBlock(actor->GetBodyOrigin()) - glm::ivec3(0, 1, 0);
    const auto stone = world.GetBlockRegistry().GetIdByTypeName("stone");
    std::vector<glm::ivec3> edits;
    for (int x = -5; x <= 5; ++x)
      for (int z = -4; z <= 10; ++z)
        for (int y = 0; y <= 5; ++y)
        {
          const auto pos = Base + glm::ivec3(x, y, z);
          const bool room =
              Request.at("light").get<std::string>().find("interior") == 0;
          const auto block = y == 0 || (room && (y == 5 || x == -5 || x == 5 ||
                                                 z == -4 || z == 10))
                                 ? stone
                                 : BLOCK_AIR;
          if (world.GetBlockWorld().GetBlock(pos) != block)
          {
            world.GetBlockWorld().SetBlock(pos, block);
            edits.push_back(pos);
          }
        }
    world.RelightPlayerEdit(edits, Base.y);
    world.InvalidateBlockMesh();
    if (Request.at("light") == "interior-lit" &&
        !world.AddObject("torch",
                         glm::vec3(Base) + glm::vec3(-.5f, 1.5f, 1.5f)))
      throw std::runtime_error("Cannot place the real room light source");
    world.SetGameMode(Request.value("mode", "creative") == "survival"
                          ? WorldGameMode::Survival
                          : WorldGameMode::Creative);
    world.SetTimeOfDayNormalized(Request.at("light") == "night" ? .85f : .35f);
    world.SetTimeFrozen(true);
    world.SetWeatherAutoEnabled(false);
    world.SetWeatherByName("clear", 0.f);
    while (camera->GetPerspective() != CameraPerspective::FirstPerson)
      camera->CycleFpsPerspective();
    if (Request.value("perspective", "first") != "first")
      camera->CycleFpsPerspective();
    if (Request.value("perspective", "first") == "front")
      camera->CycleFpsPerspective();
    camera->ResetAllKeyStatus();
    Report = {{"passed", false},
              {"scope", "actual world FP and physical drop rendering"},
              {"request", Request},
              {"frames", nlohmann::json::array()}};
    Started = Clock::now();
    SetItem(world);
  }
  void Update(UWorld &world)
  {
    if (Done)
      return;
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    std::vector<uint64_t> others;
    world.ForEachCreature(
        [&](const UCreature &c)
        {
          if (c.GetId() != actor->GetId())
            others.push_back(c.GetId());
        });
    for (auto id : others)
      world.RemoveCreature(id);
    const auto feet = glm::vec3(Base) + glm::vec3(0, .501f, 0);
    actor->SetBodyOrigin(feet);
    actor->SetOrientation(ModelYawFromCameraYaw(Yaw), Pitch);
    camera->SetPosition(feet + actor->GetEyeOffset());
    camera->SetOrientation(Yaw, Pitch);
    Ready = true;
    for (int x = -1; x <= 1; ++x)
      for (int z = -1; z <= 1; ++z)
        Ready = Ready && world.IsColumnVisualReady(glm::ivec2(
                             UChunkManager::WorldToChunk(Base).x + x,
                             UChunkManager::WorldToChunk(Base).z + z));
  }
  void RecordWorldCarry(UWorld &world)
  {
    FpWorldArms = true;
    FpRightGap = FpLeftGap = 1e9f;
    const auto contacts = TpContacts(world);
    FpContacts = contacts.size() ==
                 size_t(world.GetControlledCreature()->CarryHandCount);
    const auto camera = world.GetCurrentUserCamera();
    const auto vp = camera->GetProjection() * camera->GetViewMatrix();
    for (const auto &contact : contacts)
    {
      const auto p = contact.at("palm").get<std::array<float, 3>>();
      const auto t = contact.at("target").get<std::array<float, 3>>();
      const auto pc = vp * glm::vec4(p[0], p[1], p[2], 1);
      const auto tc = vp * glm::vec4(t[0], t[1], t[2], 1);
      if (pc.w <= .001f || tc.w <= .001f)
        continue;
      const float gap =
          glm::distance(glm::vec2(pc) / pc.w, glm::vec2(tc) / tc.w);
      (contact.at("hand") == "rightItem" ? FpRightGap : FpLeftGap) = gap;
    }
  }
  void RecordFpAudit(const FpViewmodelDrawParams &params,
                     const FpViewmodelDrawAudit &audit)
  {
    FpWorldArms = false;
    FpRightGap = FpLeftGap = 1e9f;
    FpContacts = params.HasCarryContacts;
    for (const auto &arm : audit.Arms)
    {
      const auto clip = arm.RootMvp * glm::vec4(arm.PalmLocal, 1);
      if (clip.w <= .001f)
        continue;
      const auto target = arm.Left ? params.CarryLeftNdc : params.CarryRightNdc;
      const float gap = glm::distance(glm::vec2(clip) / clip.w, target);
      (arm.Left ? FpLeftGap : FpRightGap) = gap;
    }
  }
  bool Capture(UWorld &world, UApplication &app, int width, int height)
  {
    if (Done || !Ready || Seconds(Started) < 5. ||
        Seconds(ItemStarted) < StageDelay())
      return Done;
    if (width != Request.at("width") || height != Request.at("height"))
      throw std::runtime_error("Unexpected world capture framebuffer size");
    const auto name =
        Ids[Index] + (Interaction.empty() ? "" : "-" + StageName()) + ".png";

    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < height / 2; ++y)
      std::swap_ranges(pixels.begin() + size_t(y) * width * 4,
                       pixels.begin() + size_t(y + 1) * width * 4,
                       pixels.begin() + size_t(height - 1 - y) * width * 4);
    if (!stbi_write_png((Path.parent_path() / name).string().c_str(), width,
                        height, 4, pixels.data(), width * 4))
      throw std::runtime_error("Cannot write world item capture");
    auto *actor = world.GetControlledCreature();
    const auto *active = actor->GetInventory().GetActiveEntryRef();
    const auto &env = world.GetEnvironmentState();
    const auto cell = WorldPosToBlock(actor->GetEyePosition());
    const auto coord = UChunkManager::WorldToChunk(cell);
    const auto *chunk = world.GetBlockWorld().GetChunkManager().GetChunk(coord);
    const int light =
        chunk ? int(chunk->GetLightPackedLocal(cell - coord * CHUNK_SIZE)) : -1;

    const auto bodies = UWorldItemSystem::Instance().GetItems();
    const bool bodyPresent = std::any_of(bodies.begin(), bodies.end(),
                                         [&](const auto &body)
                                         {
                                           return body.id == BodyId &&
                                                  body.stack.Id == Ids[Index] &&
                                                  body.stack.count == 1;
                                         });
    Report["frames"].push_back(
        {{"id", Ids[Index]},
         {"file", name},
         {"time", env.TimeOfDayNormalized},
         {"day_factor", env.DayNightFactor},
         {"eye_light_packed", light},
         {"active_item", active ? active->Id : std::string{}},
         {"physical_bodies", UWorldItemSystem::Instance().Count()},
         {"matching_body", bodyPresent},
         {"stage", Interaction.empty() ? "static" : StageName()},
         {"carry_hands", actor->CarryHandCount},
         {"throw_charge", actor->ThrowCharge},
         {"fp_contacts", FpContacts},
         {"fp_arm_backend", FpWorldArms ? "world_skeleton" : "viewmodel"},
         {"right_palm_ndc_gap", FpRightGap},
         {"left_palm_ndc_gap", FpLeftGap},
         {"tp_contacts", TpContacts(world)}});
    if (!Interaction.empty())
      ValidateInteraction(world);
    if (!Interaction.empty() && AdvanceInteraction(world, app))
      return false;
    ++Index;
    if (Index == Ids.size())
    {
      Done = true;
      Report["passed"] = true;
      std::ofstream(Path) << Report.dump(2) << '\n';
    }
    else
      SetItem(world);
    return Done;
  }

private:
  using Clock = std::chrono::steady_clock;
  static double Seconds(Clock::time_point time)
  {
    return std::chrono::duration<double>(Clock::now() - time).count();
  }
  std::string StageName() const
  {
    const char *names[] = {"equipped", "windup",  "canceled", "placed",
                           "grabbed",  "rewound", "released", "landed"};
    return names[Stage];
  }
  double StageDelay() const
  {
    if (Interaction.empty())
      return .45;
    const double delays[] = {.45, .25, .15, .2, .25, .45, .06, 1.3};
    return delays[Stage];
  }
  void Require(bool condition, const std::string &name)
  {
    Report["checks"].push_back(
        {{"name", Ids[Index] + " " + StageName() + " " + name},
         {"passed", condition}});
    if (!condition)
    {
      Report["passed"] = false;
      std::ofstream(Path) << Report.dump(2) << '\n';
      throw std::runtime_error("World item interaction failed: " + Ids[Index] +
                               " " + StageName() + " " + name);
    }
  }
  void ValidateInteraction(UWorld &world)
  {
    auto *actor = world.GetControlledCreature();
    const auto &inv = actor->GetInventory();
    const auto bodies = UWorldItemSystem::Instance().GetItems();
    int units = 0;
    auto inspect = [&](const InventoryEntryRef &e)
    {
      if (e.empty || e.Id != Original.Id)
        return;
      units += e.count;
      Require(e.instanceId == Original.instanceId && e.wear == Original.wear &&
                  e.broken == Original.broken &&
                  e.contentsId == Original.contentsId &&
                  e.contentsUnits == Original.contentsUnits &&
                  !e.IsCatalogReference(),
              "instance, wear and contents preserved");
    };
    for (size_t slot = 0; slot < UCreatureInventory::kSlotCount; ++slot)
      inspect(inv.GetSlot(slot));
    for (const auto &body : bodies)
      inspect(body.stack);
    Require(units == 1, "exactly one finite unit across inventory and world");
    const bool held = Stage == 1 || Stage == 2 || Stage == 4 || Stage == 5;
    Require(UWorldItemSystem::Instance().HasCargo(actor->GetId()) == held,
            "expected ownership of hands");
    if (held)
      Require(actor->CarryHandCount > 0, "hand occupancy supplied by gameplay");
    if (held && Request.value("perspective", "first") == "first")
    {
      Require(FpWorldArms, "carrying uses the actual world skeleton arms");
    }
    if (held && Request.value("perspective", "first") == "first" &&
        !FpWorldArms)
    {
      Require(FpContacts && FpRightGap < .001f,
              "rendered right palm meets cargo contact");
      if (actor->CarryHandCount == 2)
        Require(FpLeftGap < .001f, "rendered left palm meets cargo contact");
    }
    if (held &&
        (Request.value("perspective", "first") != "first" || FpWorldArms))
    {
      const auto contacts = TpContacts(world);
      Require(contacts.size() == size_t(actor->CarryHandCount),
              "both TP palms are observed");
      for (const auto &contact : contacts)
        Require(
            contact.at("gap").get<float>() < .035f,
            "TP palm meets actual carried model contact without stretching");
    }
    if (Stage == 6 || Stage == 7)
    {
      auto body = std::find_if(bodies.begin(), bodies.end(),
                               [&](const auto &b) { return b.id == BodyId; });
      Require(body != bodies.end(), "released finite body still exists");
      if (Stage == 7)
        Require(!body->flying, "released body reaches support");
    }
    if (Stage == 2)
      Require(actor->ThrowCharge == 0,
              "Escape canceled charge while preserving cargo");
    if (Stage == 1 || Stage == 5)
      Require(actor->ThrowCharge > .05f,
              "charge advanced through the game tick");
  }
  nlohmann::json TpContacts(UWorld &world)
  {
    auto *actor = world.GetControlledCreature();
    auto *visual = actor->GetVisual();
    nlohmann::json result = nlohmann::json::array();
    glm::vec3 right, left;
    if (visual && ResolveCarryContacts(world, *actor, right, left))
      for (int hand = 0; hand < actor->CarryHandCount; ++hand)
      {
        const char *name = hand ? "leftItem" : "rightItem";
        glm::mat4 frame;
        glm::vec3 low, high;
        if (!visual->GetWorldBoneTransform(name, frame) ||
            !visual->GetBoneLocalBounds(name, low, high))
          continue;
        const auto palm = glm::vec3(
            frame * glm::vec4(CreaturePalmGripPosition(low, high), 1));
        const auto target = hand ? left : right;
        glm::mat4 shoulder(1), elbow(1);
        visual->GetWorldBoneTransform(hand ? "leftArm" : "rightArm", shoulder);
        visual->GetWorldBoneTransform(hand ? "leftForearm" : "rightForearm", elbow);
        const float upper = glm::distance(glm::vec3(shoulder[3]), glm::vec3(elbow[3]));
        const float lower = glm::distance(glm::vec3(elbow[3]), palm);
        result.push_back({{"shoulder", {shoulder[3].x, shoulder[3].y, shoulder[3].z}},
                          {"target_distance", glm::distance(glm::vec3(shoulder[3]), target)},
                          {"reach_min", std::abs(upper - lower)},
                          {"reach_max", upper + lower},
                          {"hand", name},
                          {"gap", glm::distance(palm, target)},
                          {"palm", {palm.x, palm.y, palm.z}},
                          {"target", {target.x, target.y, target.z}}});
      }
    return result;
  }
  bool AdvanceInteraction(UWorld &world, UApplication &app)
  {
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    auto key = [&](int code, int action = GLFW_PRESS)
    { app.RouteKey(code, action, 0); };
    if (Stage == 0 || Stage == 4)
    {
      if (Stage == 4)
      {
        Yaw = 90;
        Pitch = -5;
      }
      key(GLFW_KEY_R);
    }
    else if (Stage == 1)
    {
      key(GLFW_KEY_ESCAPE);
      key(GLFW_KEY_R, GLFW_RELEASE);
    }
    else if (Stage == 2)
      key(GLFW_KEY_V);
    else if (Stage == 3)
    {
      const auto bodies = UWorldItemSystem::Instance().GetItems();
      auto body = std::find_if(bodies.begin(), bodies.end(),
                               [&](const auto &b) { return b.id == BodyId; });
      Require(body != bodies.end(), "placed body remains available for grab");
      const auto d = body->position - camera->GetPosition();
      Yaw = glm::degrees(std::atan2(d.z, d.x));
      Pitch = glm::degrees(std::atan2(d.y, glm::length(glm::vec2(d.x, d.z))));
      camera->SetOrientation(Yaw, Pitch);
      actor->SetOrientation(ModelYawFromCameraYaw(Yaw), Pitch);
      key(GLFW_KEY_V);
    }
    else if (Stage == 5)
    {
      key(GLFW_KEY_R, GLFW_RELEASE);
      const auto bodies = UWorldItemSystem::Instance().GetItems();
      const auto body =
          std::find_if(bodies.begin(), bodies.end(),
                       [&](const auto &b) { return b.id == BodyId; });
      Require(body != bodies.end() && !body->carrierId && body->flying &&
                  glm::length(body->velocity) > 1.f,
              "R release supplies a real flight impulse before collision "
              "resolution");
    }
    if (Stage == 7)
      return false;
    if (Stage == 0)
      for (const auto &body : UWorldItemSystem::Instance().GetItems())
        if (body.carrierId == actor->GetId())
          BodyId = body.id;
    ++Stage;
    ItemStarted = Clock::now();
    return true;
  }
  void SetItem(UWorld &world)
  {
    Stage = 0;
    Yaw = 90;
    Pitch = -12;
    auto &inv = world.GetControlledCreature()->GetInventory();
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.SetCreative(Request.value("mode", "creative") != "survival");
    world.GetControlledCreature()->GetVitals().fatigue = 0;
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Item;
    entry.Id = Ids[Index];
    entry.count = 1;
    if (!Interaction.empty())
    {
      const auto *definition = world.GetItemDefinitionStorage()->Get(entry.Id);
      entry.wear = definition && definition->StackMax == 1 ? .37f : 0.f;
      if (entry.Id == "bucket" || entry.Id == "waterskin")
      {
        entry.contentsId = "water";
        entry.contentsUnits = entry.Id == "bucket" ? 1 : 3;
      }
    }
    inv.Insert(entry);
    inv.SetActiveSlot(0, 0);
    auto &physics = UWorldItemSystem::Instance();
    physics.Clear();
    Original = *inv.GetActiveEntryRef();
    if (Interaction.empty())
      BodyId =
          physics.SpawnStack(glm::vec3(Base) + glm::vec3(-.55f, 1.1f, 2.1f),
                             entry, InventoryDomain::Creative);
    if (Interaction.empty() && !BodyId)
      throw std::runtime_error("Cannot spawn world presentation item");
    ItemStarted = Clock::now();
  }
  std::filesystem::path Path;
  nlohmann::json Request, Report;
  std::vector<std::string> Ids;
  size_t Index{0};
  int BodyId{0}, Stage{0};
  float Yaw{90}, Pitch{-12};
  std::string Interaction;
  InventoryEntryRef Original;
  glm::ivec3 Base{};
  Clock::time_point Started, ItemStarted;
  bool Ready{false}, Done{false}, FpContacts{false}, FpWorldArms{false};
  float FpRightGap{1e9f}, FpLeftGap{1e9f};
};
} // namespace cutum
