#ifndef I_CREATURE_ROLE_HANDLER_H
#define I_CREATURE_ROLE_HANDLER_H

namespace cutum
{

/// Strategy for player / mob / bot behavioral roles.
class ICreatureRoleHandler
{
public:
  virtual ~ICreatureRoleHandler() = default;

  virtual bool IsPlayer() const = 0;
  /// True when the creature can accept locomotion / influence intents.
  virtual bool CanReceiveIntent() const = 0;
  /// True when an external controller (player possession / remote) drives it.
  virtual bool IsExternallyControlled() const = 0;

  virtual void SetExternallyControlled(bool controlled) = 0;
};

} // namespace cutum

#endif
