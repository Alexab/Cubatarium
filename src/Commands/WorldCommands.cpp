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
      "time", [](const std::vector<std::string> &)
      { return CommandResult{true, "Time of day is not implemented yet."}; });

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
