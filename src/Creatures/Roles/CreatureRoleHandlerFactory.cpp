#include "Creatures/Roles/CreatureRoleHandlerFactory.h"
#include "Creatures/Roles/BotRoleHandler.h"
#include "Creatures/Roles/MobRoleHandler.h"
#include "Creatures/Roles/PlayerRoleHandler.h"

#include <algorithm>
#include <cctype>

namespace cutum
{

CreatureRoleKind ParseCreatureRoleKind(const std::string &role)
{
  std::string lower = role;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "player")
  {
    return CreatureRoleKind::Player;
  }
  if (lower == "bot")
  {
    return CreatureRoleKind::Bot;
  }
  return CreatureRoleKind::Mob;
}

std::unique_ptr<ICreatureRoleHandler>
CreateCreatureRoleHandler(CreatureRoleKind kind)
{
  switch (kind)
  {
  case CreatureRoleKind::Player:
    return std::make_unique<PlayerRoleHandler>();
  case CreatureRoleKind::Bot:
    return std::make_unique<BotRoleHandler>();
  case CreatureRoleKind::Mob:
  default:
    return std::make_unique<MobRoleHandler>();
  }
}

std::unique_ptr<ICreatureRoleHandler>
CreateCreatureRoleHandler(const std::string &role)
{
  return CreateCreatureRoleHandler(ParseCreatureRoleKind(role));
}

} // namespace cutum
