#ifndef CREATURE_ROLE_HANDLER_FACTORY_H
#define CREATURE_ROLE_HANDLER_FACTORY_H

#include "Creatures/Roles/ICreatureRoleHandler.h"
#include <memory>
#include <string>

namespace cutum
{

enum class CreatureRoleKind
{
  Player,
  Mob,
  Bot
};

CreatureRoleKind ParseCreatureRoleKind(const std::string &role);
std::unique_ptr<ICreatureRoleHandler>
CreateCreatureRoleHandler(CreatureRoleKind kind);
std::unique_ptr<ICreatureRoleHandler>
CreateCreatureRoleHandler(const std::string &role);

} // namespace cutum

#endif
