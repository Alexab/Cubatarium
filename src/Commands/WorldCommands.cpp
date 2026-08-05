#include "Commands/WorldCommands.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Game/GameSession.h"
#include "Game/WorldDifficulty.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Diagnostics/BlockInspectDiagnostics.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContentReload.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace cutum
{

namespace
{

UCreatureInventory *GetCommandInventory(const std::shared_ptr<UWorld> &world)
{
  if (!world)
  {
    return nullptr;
  }
  if (UCreature *creature = world->GetControlledCreature())
  {
    return &creature->GetInventory();
  }
  return nullptr;
}

std::string Lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                 { return static_cast<char>(std::tolower(ch)); });
  return value;
}

} // namespace

void RegisterWorldCommands(UGameSession &session, UCommandRegistry &registry)
{
  const std::shared_ptr<UWorld> world = session.GetWorld();

  registry.Register("help", [&registry](const std::vector<std::string> &)
                    { return CommandResult{true, registry.FormatHelpText()}; });

  registry.Register(
      "gamemode",
      [&session](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: gamemode <creative|survival>"};
        }
        const WorldGameMode mode = WorldGameModeFromString(args[1]);
        if (args[1] != "creative" && args[1] != "survival")
        {
          return CommandResult{false, "Unknown mode (creative|survival)"};
        }
        session.SyncToWorldGameMode(mode);
        return CommandResult{true, std::string("Game mode: ") +
                                       WorldGameModeToString(mode)};
      });

  registry.Register(
      "cheat",
      [&session](const std::vector<std::string> &args)
      {
        if (args.size() < 3 || args[1] != "inventory")
        {
          return CommandResult{
              false, "Usage: cheat inventory creative <on|off>"};
        }
        if (args[2] != "creative")
        {
          return CommandResult{false, "Unknown cheat (inventory creative)"};
        }
        bool enable = true;
        if (args.size() >= 4)
        {
          enable = args[3] == "on" || args[3] == "1" || args[3] == "true";
        }
        session.SetCheatCreativeInventory(enable);
        return CommandResult{
            true, enable ? "Creative inventory cheat ON" : "Creative inventory cheat OFF"};
      });

  registry.Register(
      "difficulty",
      [&session](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{
              false, "Usage: difficulty <peaceful|easy|normal>"};
        }
        const std::string key = Lower(args[1]);
        if (key != "peaceful" && key != "easy" && key != "normal")
        {
          return CommandResult{
              false, "Unknown difficulty (peaceful|easy|normal)"};
        }
        const WorldDifficulty difficulty = WorldDifficultyFromString(key);
        session.SyncToWorldDifficulty(difficulty);
        return CommandResult{true, std::string("Difficulty: ") +
                                       WorldDifficultyToString(difficulty)};
      });

  registry.Register(
      "worldgen",
      [&session](const std::vector<std::string> &args)
      {
        if (args.empty())
        {
          return CommandResult{
              false, "Usage: worldgen reload | worldgen debug [on|off]"};
        }
        if (args[0] == "reload")
        {
          if (!ReloadWorldGenContent())
          {
            return CommandResult{false, "Worldgen reload failed"};
          }
          return CommandResult{
              true, "Worldgen content reloaded (affects new chunks)"};
        }
        if (args[0] == "debug")
        {
          bool enable = true;
          if (args.size() >= 2)
          {
            enable = args[1] == "on" || args[1] == "1" || args[1] == "true";
          }
          const std::shared_ptr<UWorld> active = session.GetWorld();
          if (!active)
          {
            return CommandResult{false, "No active world"};
          }
          ProceduralSettings settings = active->GetProceduralSettings();
          settings.DebugWorldGenOverlay = enable;
          active->SetProceduralSettings(settings);
          return CommandResult{true, std::string("Worldgen debug overlay ") +
                                         (enable ? "enabled" : "disabled")};
        }
        return CommandResult{false, "Unknown worldgen subcommand"};
      });

  registry.Register(
      "time",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No active world"};
        }
        if (args.size() == 1)
        {
          const float t = world->GetEnvironmentState().TimeOfDayNormalized;
          std::ostringstream out;
          out << "time=" << t;
          return CommandResult{true, out.str()};
        }
        const std::string cmd = Lower(args[1]);
        if (cmd == "set")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: time set <0..1>"};
          }
          try
          {
            world->SetTimeOfDayNormalized(std::stof(args[2]));
            return CommandResult{true, "Time updated"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid time value"};
          }
        }
        if (cmd == "add")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: time add <delta>"};
          }
          try
          {
            world->AddTimeOfDayNormalized(std::stof(args[2]));
            return CommandResult{true, "Time advanced"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid delta value"};
          }
        }
        if (cmd == "freeze")
        {
          bool freeze = true;
          if (args.size() >= 3)
          {
            const std::string token = Lower(args[2]);
            freeze = token == "on" || token == "1" || token == "true";
          }
          world->SetTimeFrozen(freeze);
          return CommandResult{true, freeze ? "Time frozen" : "Time resumed"};
        }
        if (cmd == "daylength")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: time daylength <minutes>"};
          }
          try
          {
            world->SetDayLengthMinutes(std::stof(args[2]));
            return CommandResult{true, "Day length updated"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid day length"};
          }
        }
        return CommandResult{false, "Usage: time [set <0..1> | add <delta> | "
                                    "freeze [on|off] | daylength <minutes>]"};
      });

  registry.Register(
      "weather",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No active world"};
        }
        if (args.size() == 1)
        {
          return CommandResult{true, "weather=" + world->GetWeatherName()};
        }
        const std::string cmd = Lower(args[1]);
        if (cmd == "set")
        {
          if (args.size() < 3)
          {
            return CommandResult{
                false, "Usage: weather set <clear|cloudy|rain|storm|snow> "
                       "[transition_sec]"};
          }
          float transition_sec = 45.0f;
          if (args.size() >= 4)
          {
            try
            {
              transition_sec = std::stof(args[3]);
            }
            catch (...)
            {
              return CommandResult{false, "Invalid transition seconds"};
            }
          }
          UWorld::WeatherType weather = UWorld::WeatherType::Clear;
          if (!UWorld::WeatherTypeFromString(args[2], weather))
          {
            return CommandResult{
                false,
                "Unknown weather. Expected clear|cloudy|rain|storm|snow"};
          }
          world->SetWeather(weather, transition_sec);
          return CommandResult{true, "Weather transition started"};
        }
        if (cmd == "auto")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: weather auto <on|off|status>"};
          }
          const std::string sub = Lower(args[2]);
          if (sub == "on")
          {
            world->SetWeatherAutoEnabled(true);
            return CommandResult{true, "Weather auto enabled"};
          }
          if (sub == "off")
          {
            world->SetWeatherAutoEnabled(false);
            return CommandResult{true, "Weather auto disabled"};
          }
          if (sub == "status")
          {
            return CommandResult{true, world->GetWeatherAutoStatusText()};
          }
          return CommandResult{false, "Usage: weather auto <on|off|status>"};
        }
        if (cmd == "overlay")
        {
          bool enabled = true;
          if (args.size() >= 3)
          {
            const std::string token = Lower(args[2]);
            enabled = token == "on" || token == "1" || token == "true";
          }
          world->SetWeatherOverlayEnabled(enabled);
          return CommandResult{true, enabled ? "Weather overlay enabled"
                                             : "Weather overlay disabled"};
        }
        if (cmd == "particles")
        {
          bool enabled = true;
          if (args.size() >= 3)
          {
            const std::string token = Lower(args[2]);
            enabled = token == "on" || token == "1" || token == "true";
          }
          world->SetWeatherParticlesEnabled(enabled);
          return CommandResult{true, enabled ? "Weather particles enabled"
                                             : "Weather particles disabled"};
        }
        if (cmd == "debug")
        {
          if (args.size() < 3)
          {
            return CommandResult{
                false,
                "Usage: weather debug <0|1|2|3> (off|solid|depth|particles)"};
          }
          try
          {
            const int mode = std::stoi(args[2]);
            if (mode < 0 || mode > 3)
            {
              return CommandResult{false, "Weather debug mode must be 0-3"};
            }
            world->SetWeatherDebugMode(static_cast<uint8_t>(mode));
            return CommandResult{true, "Weather debug mode set"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid weather debug mode"};
          }
        }
        return CommandResult{false, "Usage: weather [set <type> "
                                    "[transition_sec] | auto <on|off|status> | "
                                    "overlay [on|off] | "
                                    "particles [on|off] | debug <0-3>]"};
      });

  registry.Register(
      "sky",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No active world"};
        }
        if (args.size() < 2)
        {
          return CommandResult{
              false,
              "Usage: sky <stars <0..1>|clouds <0..1>|reset_celestials>"};
        }
        const std::string cmd = Lower(args[1]);
        if (cmd == "stars")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: sky stars <0..1>"};
          }
          try
          {
            world->SetStarVisibility(std::stof(args[2]));
            return CommandResult{true, "Sky stars visibility updated"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid stars value"};
          }
        }
        if (cmd == "clouds")
        {
          if (args.size() < 3)
          {
            return CommandResult{false, "Usage: sky clouds <0..1>"};
          }
          try
          {
            world->SetCloudCoverage(std::stof(args[2]));
            return CommandResult{true, "Sky cloud coverage updated"};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid clouds value"};
          }
        }
        if (cmd == "reset_celestials")
        {
          world->ResetCelestialBodies();
          return CommandResult{true, "Sky celestial bodies reset"};
        }
        return CommandResult{
            false, "Usage: sky <stars <0..1>|clouds <0..1>|reset_celestials>"};
      });

  registry.Register(
      "light",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No active world"};
        }
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: light <recalc|debug>"};
        }
        const std::string cmd = Lower(args[1]);
        if (cmd == "recalc")
        {
          world->RebuildAllLightingDirtyMeshes();
          return CommandResult{true, "Scheduled full light mesh refresh"};
        }
        if (cmd == "debug")
        {
          if (args.size() < 3)
          {
            return CommandResult{
                false, "Usage: light debug <off|on|sky|block|combined>"};
          }
          const std::string token = Lower(args[2]);
          if (token == "off" || token == "0" || token == "false")
          {
            world->SetLightingDebugMode(0);
            world->RebuildAllLightingDirtyMeshes();
            return CommandResult{true, "Light debug disabled"};
          }
          uint8_t mode = 1;
          std::string label = "combined";
          if (token == "sky")
          {
            mode = 2;
            label = "sky";
          }
          else if (token == "block")
          {
            mode = 3;
            label = "block";
          }
          else if (token == "combined" || token == "on" || token == "1" ||
                   token == "true")
          {
            mode = 1;
            label = "combined";
          }
          else
          {
            return CommandResult{
                false, "Usage: light debug <off|on|sky|block|combined>"};
          }
          world->SetLightingDebugMode(mode);
          world->RebuildAllLightingDirtyMeshes();
          return CommandResult{true, "Light debug: " + label};
        }
        return CommandResult{
            false, "Usage: light <recalc|debug <off|on|sky|block|combined>>"};
      });

  registry.Register("give",
                    [world](const std::vector<std::string> &args)
                    {
                      if (args.size() < 2)
                      {
                        return CommandResult{false, "Usage: give <id>"};
                      }
                      if (UCreatureInventory *inv = GetCommandInventory(world))
                      {
                        const std::string &id = args[1];
                        if (world->GetItemDefinitionStorage() &&
                            world->GetItemDefinitionStorage()->Get(id))
                        {
                          InventoryEntryRef entry;
                          entry.empty = false;
                          entry.kind = InventoryEntryKind::Item;
                          entry.Id = id;
                          entry.count = 1;
                          entry.wear = 0.f;
                          entry.broken = false;
                          const size_t bar = inv->GetActiveBarIndex();
                          const size_t slot = inv->GetActiveSlotIndex();
                          inv->AssignToHotbar(bar, slot, entry);
                          return CommandResult{true, "Gave tool " + id};
                        }
                        inv->AddToInventory(id);
                      }
                      else
                      {
                        return CommandResult{false, "No controlled creature"};
                      }
                      return CommandResult{true, "Added " + args[1]};
                    });

  registry.Register(
      "repair",
      [world](const std::vector<std::string> &args)
      {
        (void)args;
        UCreature *creature = world->GetControlledCreature();
        if (!creature)
        {
          return CommandResult{false, "No controlled creature"};
        }
        UItemDefinitionStorage *items = world->GetItemDefinitionStorage();
        if (!items)
        {
          return CommandResult{false, "No item definitions"};
        }
        auto &bars = creature->GetInventory().GetHotbarsMutable();
        const size_t bar = creature->GetInventory().GetActiveBarIndex();
        const size_t slot = creature->GetInventory().GetActiveSlotIndex();
        if (bar >= bars.size() || slot >= bars[bar].slots.size() ||
            bars[bar].slots[slot].empty)
        {
          return CommandResult{false, "Empty active slot"};
        }
        auto &entry = bars[bar].slots[slot].entry;
        if (entry.kind != InventoryEntryKind::Item)
        {
          return CommandResult{false, "Active slot is not an item"};
        }
        const ItemDefinition *def = items->Get(entry.Id);
        if (!def)
        {
          return CommandResult{false, "Unknown item"};
        }
        const std::string material =
            def->Repair.Materials.empty() ? std::string{}
                                          : def->Repair.Materials.front();
        if (!TryRepairItem(entry, *def, material))
        {
          return CommandResult{false, "Repair failed"};
        }
        return CommandResult{true, "Repaired " + entry.Id};
      });

  registry.Register(
      "tp",
      [world](const std::vector<std::string> &args)
      {
        if (args.size() < 4)
        {
          return CommandResult{false, "Usage: tp <x> <y> <z>"};
        }
        auto user = world->GetCurrentUser();
        auto camera = world->GetCurrentUserCamera();
        UCreature *controlled = world->GetControlledCreature();
        if (!user || !camera)
        {
          return CommandResult{false, "No user/camera"};
        }
        try
        {
          const float x = std::stof(args[1]);
          const float y = std::stof(args[2]);
          const float z = std::stof(args[3]);
          const glm::vec3 eye{x, y, z};
          user->SetPosition(eye);
          camera->SetPosition(eye);
          if (controlled)
          {
            controlled->SetBodyOrigin(glm::vec3(
                eye.x, FeetYFromEye(eye, controlled->GetEyeOffset().y), eye.z));
          }
          return CommandResult{true, "Teleported"};
        }
        catch (...)
        {
          return CommandResult{false, "Invalid coordinates"};
        }
      });

  registry.Register(
      "fly",
      [world](const std::vector<std::string> &args)
      {
        CreatureHabitat habitat = CreatureHabitat::Terrestrial;
        if (UCreature *controlled = world->GetControlledCreature())
        {
          if (const CreatureDefinition *def =
                  world->GetCreatureDefinition(controlled->GetTypeId()))
          {
            habitat = def->habitat;
          }
        }
        if (!ModePolicy::AllowsFlight(world->GetGameMode(), habitat))
        {
          return CommandResult{false,
                               "Creative fly disabled in Survival mode"};
        }
        auto camera = world->GetCurrentUserCamera();
        if (!camera)
        {
          return CommandResult{false, "No camera"};
        }
        bool enable = true;
        if (args.size() >= 2)
        {
          enable = args[1] == "on" || args[1] == "1" || args[1] == "true";
        }
        camera->SetFreeMove(enable);
        if (UCreature *c = world->GetControlledCreature())
        {
          c->GetLocomotion().SetMode(enable ? CreatureMovementMode::Flying
                                            : CreatureMovementMode::Walking);
        }
        return CommandResult{true, enable ? "Flight on" : "Flight off"};
      });

  registry.Register(
      "spawn",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No world"};
        }
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: spawn <species> [skin]"};
        }
        const std::string &species = args[1];
        const CreatureDefinition *def = world->GetCreatureDefinition(species);
        if (!def)
        {
          return CommandResult{false, "Unknown species: " + species};
        }
        if (!def->catalog.spawnable)
        {
          return CommandResult{false, "Species is not spawnable: " + species};
        }
        const std::string skin = args.size() >= 3 ? args[2] : "";
        if (!skin.empty())
        {
          const auto &skinStorage = world->GetSkinDefinitionStorage();
          if (!skinStorage || !skinStorage->IsCompatible(skin, species))
          {
            return CommandResult{false, "Skin is not compatible with species"};
          }
        }
        const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);
        glm::vec3 bodyOrigin = world->GetSpawnPoint();
        bodyOrigin.y -= eyeOffset.y;
        if (auto camera = world->GetCurrentUserCamera())
        {
          glm::vec3 forward = camera->GetFront();
          forward.y = 0.0f;
          if (glm::length(forward) > 0.01f)
          {
            bodyOrigin = camera->GetPosition() - eyeOffset +
                         glm::normalize(forward) * 3.0f;
          }
          else
          {
            bodyOrigin =
                camera->GetPosition() - eyeOffset + glm::vec3(3.0f, 0.0f, 0.0f);
          }
        }
        const CreatureId Id = world->SpawnCreature(species, bodyOrigin, skin);
        if (Id == 0)
        {
          return CommandResult{false, "Spawn failed"};
        }
        return CommandResult{true, "Spawned " + species +
                                       " Id=" + std::to_string(Id)};
      });

  registry.Register(
      "select_skin",
      [world](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{false, "Usage: select_skin <skin_id>"};
        }
        auto user = world->GetCurrentUser();
        UCreature *controlled = world->GetControlledCreature();
        if (!user || !controlled)
        {
          return CommandResult{false, "No controlled creature"};
        }
        std::string error;
        if (!world->TryApplySkin(controlled->GetId(), args[1], &error))
        {
          return CommandResult{false, error};
        }
        user->SetSelectedSkinId(args[1]);
        return CommandResult{true, "Skin set to " + args[1]};
      });

  registry.Register("apply_skin",
                    [world](const std::vector<std::string> &args)
                    {
                      if (!world)
                      {
                        return CommandResult{false, "No world"};
                      }
                      if (args.size() < 2)
                      {
                        return CommandResult{false,
                                             "Usage: apply_skin <skin_id>"};
                      }
                      auto camera = world->GetCurrentUserCamera();
                      if (!camera)
                      {
                        return CommandResult{false, "No camera"};
                      }
                      const auto target = world->PickCreatureByView(
                          camera->GetPosition(), camera->GetFront(), 8.0f);
                      if (!target)
                      {
                        return CommandResult{false, "No creature in view"};
                      }
                      std::string error;
                      if (!world->TryApplySkin(*target, args[1], &error))
                      {
                        return CommandResult{false, error};
                      }
                      return CommandResult{true, "Applied skin " + args[1]};
                    });

  registry.Register(
      "possess",
      [world](const std::vector<std::string> &args)
      {
        if (!world)
        {
          return CommandResult{false, "No world"};
        }
        CreatureId target = 0;
        if (args.size() >= 2)
        {
          try
          {
            target = static_cast<CreatureId>(std::stoull(args[1]));
          }
          catch (...)
          {
            return CommandResult{false, "Invalid creature Id"};
          }
        }
        else
        {
          world->ForEachCreature(
              [&](UCreature &c)
              {
                if (target == 0 && !c.IsPlayerCharacter())
                {
                  target = c.GetId();
                }
              });
        }
        if (target == 0 || !world->SetControlledCreature(target))
        {
          return CommandResult{false, "Cannot possess"};
        }
        if (auto cam = world->GetCurrentUserCamera())
        {
          if (UCreature *c = world->GetCreature(target))
          {
            cam->SetPosition(c->GetEyePosition());
            cam->SetOrientation(CameraYawFromModelYaw(c->GetYaw()),
                                c->GetPitch());
          }
        }
        return CommandResult{true, "Possessing Id=" + std::to_string(target)};
      });

  registry.Register(
      "depossess",
      [world](const std::vector<std::string> &)
      {
        if (!world)
        {
          return CommandResult{false, "No world"};
        }
        const CreatureId playerId = world->GetPlayerCreatureId();
        if (playerId == 0 || !world->SetControlledCreature(playerId))
        {
          return CommandResult{false, "No player creature"};
        }
        if (auto cam = world->GetCurrentUserCamera())
        {
          if (UCreature *c = world->GetPlayerCreature())
          {
            cam->SetPosition(c->GetEyePosition());
            cam->SetOrientation(CameraYawFromModelYaw(c->GetYaw()),
                                c->GetPitch());
          }
        }
        return CommandResult{true, "Returned to player"};
      });

  registry.Register(
      "select_appearance",
      [world](const std::vector<std::string> &args)
      {
        if (args.size() < 2)
        {
          return CommandResult{
              false, "Usage: select_appearance <skin_id> (alias: select_skin)"};
        }
        auto user = world->GetCurrentUser();
        UCreature *controlled = world->GetControlledCreature();
        if (!user || !controlled)
        {
          return CommandResult{false, "No controlled creature"};
        }
        std::string error;
        if (!world->TryApplySkin(controlled->GetId(), args[1], &error))
        {
          return CommandResult{false, error};
        }
        user->SetSelectedSkinId(args[1]);
        user->SetSelectedAppearanceTypeId(args[1]);
        return CommandResult{true, "Appearance set to " + args[1]};
      });

  registry.Register(
      "block",
      [world](const std::vector<std::string> &args) -> CommandResult
      {
        if (args.size() < 2 || args[1] != "inspect")
        {
          return CommandResult{
              false,
              "Usage: block inspect | block inspect clear | block inspect path"};
        }
        if (args.size() >= 3 && args[2] == "clear")
        {
          UBlockInspectDiagnostics::ClearLog();
          return CommandResult{true, "block_inspect.jsonl cleared"};
        }
        if (args.size() >= 3 && args[2] == "path")
        {
          return CommandResult{
              true, UBlockInspectDiagnostics::DefaultLogPath().string()};
        }
        if (!world)
        {
          return CommandResult{false, "No world"};
        }
        const int sample =
            UBlockInspectDiagnostics::CaptureFromCrosshair(*world, nullptr);
        if (sample < 0)
        {
          return CommandResult{false, "No block under crosshair"};
        }
        return CommandResult{
            true,
            "Block inspect logged (#" + std::to_string(sample) + ") -> " +
                UBlockInspectDiagnostics::DefaultLogPath().string()};
      });

  registry.Register(
      "creature_diag",
      [world](const std::vector<std::string> &args) -> CommandResult
      {
        if (args.size() < 2)
        {
          return CommandResult{
              false,
              "Usage: creature_diag on|off|clear|path|dump|flush|verbose "
              "on|off|focus <id|nearest|clear>"};
        }
        const std::string sub = Lower(args[1]);
        if (sub == "on")
        {
          UCreatureMovementDiagnostics::SetEnabled(true);
          return CommandResult{true, "creature_movement_diag on"};
        }
        if (sub == "off")
        {
          UCreatureMovementDiagnostics::SetEnabled(false);
          return CommandResult{true, "creature_movement_diag off"};
        }
        if (sub == "verbose")
        {
          if (args.size() < 3)
          {
            return CommandResult{
                false, "Usage: creature_diag verbose on|off"};
          }
          const std::string mode = Lower(args[2]);
          if (mode == "on" || mode == "1" || mode == "true")
          {
            UCreatureMovementDiagnostics::SetVerbose(true);
            return CommandResult{true, "creature_diag verbose on"};
          }
          if (mode == "off" || mode == "0" || mode == "false")
          {
            UCreatureMovementDiagnostics::SetVerbose(false);
            return CommandResult{true, "creature_diag verbose off"};
          }
          return CommandResult{false, "Usage: creature_diag verbose on|off"};
        }
        if (sub == "clear")
        {
          UCreatureMovementDiagnostics::ClearLog();
          return CommandResult{true, "creature_movement_diag.jsonl cleared"};
        }
        if (sub == "flush")
        {
          UCreatureMovementDiagnostics::Flush();
          return CommandResult{true, "creature_movement_diag flushed"};
        }
        if (sub == "path")
        {
          return CommandResult{
              true, UCreatureMovementDiagnostics::DefaultLogPath().string()};
        }
        if (sub == "dump")
        {
          if (!UCreatureMovementDiagnostics::DumpRing())
          {
            return CommandResult{false, "dump failed"};
          }
          return CommandResult{
              true, "dumped ring (" +
                        std::to_string(UCreatureMovementDiagnostics::GetRingSize()) +
                        " samples)"};
        }
        if (sub == "focus")
        {
          if (args.size() < 3)
          {
            return CommandResult{
                false, "Usage: creature_diag focus <id|nearest|clear>"};
          }
          const std::string target = Lower(args[2]);
          if (target == "clear" || target == "0" || target == "all")
          {
            UCreatureMovementDiagnostics::SetFocusId(0);
            return CommandResult{true, "creature_diag focus cleared (all)"};
          }
          if (target == "nearest")
          {
            if (!world)
            {
              return CommandResult{false, "No world"};
            }
            const UCreature *controlled = world->GetControlledCreature();
            if (!controlled)
            {
              return CommandResult{false, "No controlled creature"};
            }
            const glm::vec3 origin = controlled->GetBodyOrigin();
            CreatureId best_id = 0;
            float best_dist_sq = 1.0e30f;
            world->ForEachCreature(
                [&](const UCreature &creature)
                {
                  if (creature.GetId() == controlled->GetId() ||
                      creature.IsPlayerCharacter())
                  {
                    return;
                  }
                  const glm::vec3 d = creature.GetBodyOrigin() - origin;
                  const float dist_sq = glm::dot(d, d);
                  if (dist_sq < best_dist_sq)
                  {
                    best_dist_sq = dist_sq;
                    best_id = creature.GetId();
                  }
                });
            if (best_id == 0)
            {
              return CommandResult{false, "No nearby creatures"};
            }
            UCreatureMovementDiagnostics::SetFocusId(best_id);
            return CommandResult{
                true, "creature_diag focus=" + std::to_string(best_id)};
          }
          try
          {
            const uint64_t id = static_cast<uint64_t>(std::stoull(args[2]));
            UCreatureMovementDiagnostics::SetFocusId(id);
            return CommandResult{
                true, "creature_diag focus=" + std::to_string(id)};
          }
          catch (...)
          {
            return CommandResult{false, "Invalid creature id"};
          }
        }
        return CommandResult{
            false,
            "Usage: creature_diag on|off|clear|path|dump|flush|verbose "
            "on|off|focus <id|nearest|clear>"};
      });
}

} // namespace cutum
