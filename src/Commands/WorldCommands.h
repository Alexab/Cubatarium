#pragma once

#include "Commands/CommandRegistry.h"

namespace cutum
{

class UGameSession;

void RegisterWorldCommands(UGameSession &session, UCommandRegistry &registry);

} // namespace cutum
