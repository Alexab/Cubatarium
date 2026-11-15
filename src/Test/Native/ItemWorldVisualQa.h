#pragma once
#include "App/Application.h"
#include "App/Platform/InputManager.h"
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
    Subject = Request.value("subject", "item");
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
          if (c.GetId() != actor->GetId() && c.GetId() != CargoCreature)
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
          if (c.GetId() != actor->GetId() && c.GetId() != CargoCreature)
            others.push_back(c.GetId());
        });
    for (auto id : others)
      world.RemoveCreature(id);
    const auto feet = glm::vec3(Base) + glm::vec3(0, .501f, 0);
    actor->SetBodyOrigin(feet);
    if (Subject == "creature" && Stage == 0)
      if (auto *target = world.GetCreature(CargoCreature))
      {
        target->SetBodyOrigin(feet + glm::vec3(0, 0, 1.7f));
        const auto d = target->GetCollisionVolume().center -
                       (feet + actor->GetEyeOffset());
        Yaw = glm::degrees(std::atan2(d.z, d.x));
        Pitch = glm::degrees(std::atan2(d.y, glm::length(glm::vec2(d.x, d.z))));
      }
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
    FpItems = nlohmann::json::array();
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
    FpItems = nlohmann::json::array();
    for (const auto &item : audit.Items)
      FpItems.push_back({{"id", item.Id}, {"bow_charge", item.Inputs.BowCharge},
                         {"use_phase", item.Inputs.UsePhase},
                         {"contents_units", item.Inputs.ContentsUnits},
                         {"has_water", item.Inputs.HasWater},
                         {"has_ammo", item.Inputs.HasAmmo},
                         {"visible_primitives", item.VisiblePrimitives}});
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
                                           return Subject == "creature"
                                                      ? body.creatureId == CargoCreature
                                                      : body.id == BodyId &&
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
         {"subject", Subject},
         {"creature_id", CargoCreature},
         {"physical_bodies", UWorldItemSystem::Instance().Count()},
         {"matching_body", bodyPresent},
         {"stage", Interaction.empty() ? "static" : StageName()},
         {"carry_hands", actor->CarryHandCount},
         {"throw_charge", actor->ThrowCharge},
         {"bow_charge", actor->BowCharge},
         {"use_phase", actor->ActiveItemUsePhase()},
         {"active_count", active ? active->count : 0},
         {"contents_units", active ? active->contentsUnits : 0},
         {"fp_items", FpItems},
         {"fp_contacts", FpContacts},
         {"fp_arm_backend", FpWorldArms ? "world_skeleton" : "viewmodel"},
         {"right_palm_ndc_gap", FpRightGap},
         {"left_palm_ndc_gap", FpLeftGap},
         {"tp_contacts", TpContacts(world)}});
    if (Subject == "creature")
      ValidateCreature(world);
    else if (Interaction == "use")
      ValidateUse(world);
    else if (!Interaction.empty())
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
    if (Interaction == "use")
    {
      const char *names[] = {"equipped", "partial", "canceled", "restored",
                             "ready", "completed", "restarted", "canceled-again"};
      return names[Stage];
    }
    const char *names[] = {"equipped", "windup",  "canceled", "placed",
                           "grabbed",  "rewound", "released", "landed"};
    return names[Stage];
  }
  double StageDelay() const
  {
    if (Interaction.empty())
      return .45;
    if (Interaction == "use")
      return Stage == 4 ? UseDuration + .15 : Stage == 1 || Stage == 6 ? .18 : .2;
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
  void ValidateUse(UWorld &world)
  {
    auto *actor = world.GetControlledCreature();
    const auto &inv = actor->GetInventory();
    const auto *definition = world.GetItemDefinitionStorage()->Get(Original.Id);
    const bool ranged = definition->Ranged.Enabled;
    const bool finiteCost = !inv.IsCreative() && Stage >= 5;
    const bool container = Original.contentsUnits > 0;
    const int expectedCount = Original.count - (finiteCost && !ranged && !container ? 1 : 0);
    Require(inv.Count(InventoryEntryKind::Item, Original.Id) == expectedCount,
            "use changes finite count only on successful completion");
    const auto &entry = inv.GetSlot(0);
    Require(entry.instanceId == Original.instanceId && entry.Id == Original.Id,
            "use preserves the selected stack/instance identity");
    if (container)
      Require(entry.contentsUnits == Original.contentsUnits - (finiteCost ? 1 : 0),
              "drink changes contents exactly once and respects mode policy");
    if (ranged)
    {
      Require(inv.Count(InventoryEntryKind::Item, "arrow") == 3 - (finiteCost ? 1 : 0),
              "bow spends ammunition exactly once and respects mode policy");
      size_t arrows = 0;
      for (const auto &body : UWorldItemSystem::Instance().GetItems())
        if (!body.creatureId && body.stack.Id == definition->Ranged.AmmoId)
          arrows += size_t(body.stack.count);
      Require(arrows == (Stage >= 5 ? 1u : 0u),
              "only the completed bow input creates a persistent arrow");
    }
    const float phase = ranged ? actor->BowCharge : actor->ActiveItemUsePhase();
    if (Stage == 1 || Stage == 6)
      Require(phase > .01f && phase < 1.f, "held input advances a partial visual phase");
    else if (Stage == 4)
      Require(phase >= .999f, "fully held input reaches its ready phase");
    else
      Require(phase == 0.f, "idle, completion and cancellation clear the use phase");
    if (Stage == 2 || Stage == 7)
      Require(!inv.GetActiveEntryRef() || inv.GetActiveEntryRef()->empty,
              "routed hotbar key selects the empty cancellation slot");
    else if (Request.value("perspective", "first") == "first")
    {
      bool rendered = false;
      for (const auto &item : FpItems)
        if (item.at("id") == Original.Id)
        {
          rendered = item.at("visible_primitives").get<size_t>() > 0;
          Require(std::abs(item.at(ranged ? "bow_charge" : "use_phase").get<float>() - phase) < .001f,
                  "actual FP draw consumes the authoritative use phase");
          if (container)
            Require(item.at("contents_units") == entry.contentsUnits && item.at("has_water") == true,
                    "actual FP draw consumes current container contents");
        }
      Require(rendered, "active item is drawn during real use input");
    }
  }
  bool AdvanceUse()
  {
#if defined(__ANDROID__)
    throw std::runtime_error("Use capture requires desktop GLFW input callbacks");
#else
    auto *window = glfwGetCurrentContext();
    if (!window)
      throw std::runtime_error("Use capture has no current window");
    int width = 0, height = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwSetCursorPos(window, width * .5, height * .5);
    auto mouse = [&](bool pressed)
    {
      UInputManager::GLFWMouseButtonCallback(
          window, GLFW_MOUSE_BUTTON_RIGHT, pressed ? GLFW_PRESS : GLFW_RELEASE, 0);
    };
    auto key = [&](int code)
    {
      UInputManager::GLFWKeyCallback(window, code, 0, GLFW_PRESS, 0);
      UInputManager::GLFWKeyCallback(window, code, 0, GLFW_RELEASE, 0);
    };
    if (Stage == 0 || Stage == 3 || Stage == 5)
      mouse(true);
    else if (Stage == 1 || Stage == 6)
    {
      key(GLFW_KEY_2);
      mouse(false);
    }
    else if (Stage == 2)
      key(GLFW_KEY_1);
    else if (Stage == 4)
    {
      mouse(false);
      if (Original.Id == "wood_bow")
      {
        bool launched = false;
        for (const auto &body : UWorldItemSystem::Instance().GetItems())
          launched |= body.arrow && body.flying && body.throwerId != 0;
        Require(launched, "mouse release creates a flying arrow before collision resolution");
      }
    }
    if (Stage == 7)
      return false;
    ++Stage;
    ItemStarted = Clock::now();
    return true;
#endif
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
    if (Interaction == "use")
      return AdvanceUse();
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
      if (Stage == 0 && Subject == "creature")
        key(GLFW_KEY_V);
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
      glm::vec3 center;
      if (Subject == "creature")
      {
        const auto *target = world.GetCreature(CargoCreature);
        Require(target != nullptr, "placed creature remains in its registry");
        center = target->GetCollisionVolume().center;
      }
      else
      {
        Require(body != bodies.end(), "placed body remains available for grab");
        center = body->position;
      }
      const auto d = center - camera->GetPosition();
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
    if (Stage == 0 || Stage == 3)
      for (const auto &body : UWorldItemSystem::Instance().GetItems())
        if (body.carrierId == actor->GetId())
          BodyId = body.id;
    ++Stage;
    ItemStarted = Clock::now();
    return true;
  }
  void PrepareCreature(UWorld &world)
  {
    if (CargoCreature)
      world.RemoveCreature(CargoCreature);
    auto &physics = UWorldItemSystem::Instance();
    physics.Clear();
    auto *actor = world.GetControlledCreature();
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
    const auto mode = world.GetGameMode();
    world.SetGameMode(WorldGameMode::Creative);
    CargoCreature = world.SpawnCreature(
        Ids[Index], glm::vec3(Base) + glm::vec3(0, .501f, 1.7f),
        "", CreatureSpawnSource::Qa);
    world.SetGameMode(mode);
    auto *target = world.GetCreature(CargoCreature);
    if (!target)
      throw std::runtime_error("Cannot spawn QA cargo creature: " + Ids[Index]);
    auto &inventory = target->GetInventory();
    inventory = UCreatureInventory{};
    inventory.SetItemDefinitions(world.GetItemDefinitionStorage());
    inventory.SetCreative(mode == WorldGameMode::Creative);
    InventoryEntryRef seed;
    seed.empty = false;
    seed.kind = InventoryEntryKind::Item;
    seed.Id = "seeds";
    seed.count = 1;
    inventory.Insert(seed);
    CargoHealth = target->GetVitals().health;
  }
  void ValidateCreature(UWorld &world)
  {
    auto *actor = world.GetControlledCreature();
    const auto *target = world.GetCreature(CargoCreature);
    const bool held = Stage == 1 || Stage == 2 || Stage == 4 || Stage == 5;
    Require(UWorldItemSystem::Instance().HasCargo(actor->GetId()) == held,
            "creature hand ownership follows the routed input");
    int seeds = actor->GetInventory().Count(InventoryEntryKind::Item, "seeds");
    if (target)
      seeds += target->GetInventory().Count(InventoryEntryKind::Item, "seeds");
    for (const auto &body : UWorldItemSystem::Instance().GetItems())
      if (!body.creatureId && body.stack.Id == "seeds")
        seeds += body.stack.count;
    Require(seeds == 1, "finite creature property survives carry/release/death");
    Report["frames"].back()["creature_present"] = target != nullptr;
    Report["frames"].back()["creature_health"] = target ? target->GetVitals().health : 0;
    if (Stage < 6)
      Require(target && target->GetId() == CargoCreature &&
                  target->GetVitals().health == CargoHealth,
              "grab and gentle placement preserve the existing living creature");
    if (held)
    {
      Require(target && target->ExternalMotion,
              "held creature yields locomotion to the physical link");
      Require(actor->CarryHandCount == (Ids[Index] == "rat" ? 1 : 2),
              "small and larger prototypes use one and two hands");
      if (Request.value("perspective", "first") == "first")
        Require(FpWorldArms, "creature cargo uses shared world-skeleton FP arms");
      const auto contacts = TpContacts(world);
      Require(contacts.size() == size_t(actor->CarryHandCount), "creature palms are observed");
      for (const auto &contact : contacts)
        Require(contact.at("gap").get<float>() < .035f,
                "creature palm reaches its contact without stretching");
    }
    if (Stage == 7 && target && target->GetVitals().health > 0)
      Require(!target->ExternalMotion, "surviving creature returns to AI locomotion");
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
    entry.kind = Subject == "block" ? InventoryEntryKind::Block : InventoryEntryKind::Item;
    entry.Id = Ids[Index];
    if (Subject == "creature")
    {
      PrepareCreature(world);
      ItemStarted = Clock::now();
      return;
    }
    entry.count = 1;
    if (Interaction == "use")
    {
      const auto *definition = world.GetItemDefinitionStorage()->Get(entry.Id);
      if (!definition || (!definition->Ranged.Enabled &&
          definition->Use.Action != ItemUseActionKind::Eat &&
          definition->Use.Action != ItemUseActionKind::Drink))
        throw std::runtime_error("Use capture supports food, drinks and bows");
      UseDuration = definition->Ranged.Enabled ? 1.1f : definition->Use.Duration;
      if (definition->StackMax > 1)
        entry.count = 3;
      auto &vitals = world.GetControlledCreature()->GetVitals();
      vitals.satiety = 20;
      vitals.thirst = 20;
      world.GetControlledCreature()->AdvanceInfluenceCooldown(10.f);
    }
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
    if (Interaction == "use" && world.GetItemDefinitionStorage()->Get(entry.Id)->Ranged.Enabled)
    {
      InventoryEntryRef ammo;
      ammo.empty = false;
      ammo.kind = InventoryEntryKind::Item;
      ammo.Id = "arrow";
      ammo.count = 3;
      inv.Insert(ammo);
      // Keep slot 2 empty: it is the real hotbar cancellation target.
      if (!inv.GetSlot(1).empty)
        inv.Move(1, 2);
    }
    if (Interaction.empty())
      BodyId =
          physics.SpawnStack(glm::vec3(Base) + glm::vec3(-.55f, 1.1f, 2.1f),
                             entry, InventoryDomain::Creative);
    if (Interaction.empty() && !BodyId)
      throw std::runtime_error("Cannot spawn world presentation item");
    ItemStarted = Clock::now();
  }
  std::filesystem::path Path;
  nlohmann::json Request, Report, FpItems = nlohmann::json::array();
  std::vector<std::string> Ids;
  size_t Index{0};
  int BodyId{0}, Stage{0};
  float Yaw{90}, Pitch{-12}, UseDuration{.8f};
  std::string Interaction, Subject;
  uint64_t CargoCreature{0};
  float CargoHealth{0};
  InventoryEntryRef Original;
  glm::ivec3 Base{};
  Clock::time_point Started, ItemStarted;
  bool Ready{false}, Done{false}, FpContacts{false}, FpWorldArms{false};
  float FpRightGap{1e9f}, FpLeftGap{1e9f};
};
} // namespace cutum
