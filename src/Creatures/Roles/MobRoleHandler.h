#ifndef MOB_ROLE_HANDLER_H
#define MOB_ROLE_HANDLER_H

#include "Creatures/Roles/ICreatureRoleHandler.h"

namespace cutum
{

class MobRoleHandler : public ICreatureRoleHandler
{
public:
  bool IsPlayer() const override { return false; }
  bool CanReceiveIntent() const override { return true; }
  bool IsExternallyControlled() const override { return ExternallyControlled; }
  void SetExternallyControlled(bool controlled) override
  {
    ExternallyControlled = controlled;
  }

private:
  bool ExternallyControlled{false};
};

} // namespace cutum

#endif
