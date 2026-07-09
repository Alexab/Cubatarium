#include "Commands/WorldCommands.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Game/GameSession.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
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
            world->RebuildAllLightingDirtyMeshes();
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
            world->RebuildAllLightingDirtyMeshes();
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
            return CommandResult{
                false, "Usage: weather auto <on|off|status>"};
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
        return CommandResult{
            false,
            "Usage: weather [set <type> [transition_sec] | auto <on|off|status> | "
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
          bool enabled = true;
          if (args.size() >= 3)
          {
            const std::string token = Lower(args[2]);
            enabled = token == "on" || token == "1" || token == "true";
          }
          world->SetLightingDebugEnabled(enabled);
          world->RebuildAllLightingDirtyMeshes();
          return CommandResult{true, enabled ? "Light debug enabled"
                                             : "Light debug disabled"};
        }
        return CommandResult{false, "Usage: light <recalc|debug [on|off]>"};
      });

  registry.Register("give",
                    [world](const std::vector<std::string> &args)
                    {
                      if (args.size() < 2)
                      {
                        return CommandResult{false, "Usage: give <block>"};
                      }
                      if (UCreatureInventory *inv = GetCommandInventory(world))
                      {
                        inv->AddToInventory(args[1]);
                      }
                      else
                      {
                        return CommandResult{false, "No controlled creature"};
                      }
                      return CommandResult{true, "Added " + args[1]};
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
}

} // namespace cutum
