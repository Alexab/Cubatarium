#include "App/ResourcePackSelectionUtil.h"

#include "App/Core.h"
#include "App/ResourcePackBootstrap.h"
#include <algorithm>

namespace cutum
{

ResourcePackSelection NormalizeResourcePackSelection(
    const UCore &core, const UResourcePackBootstrap &bootstrap,
    const ResourcePackSelection &requested)
{
  ResourcePackSelection selection = requested;
  selection.Primary =
      bootstrap.NormalizeEnabledPackIds(core, selection.Primary);
  selection.Secondary =
      bootstrap.NormalizeEnabledPackIds(core, selection.Secondary);
  if (selection.Primary.empty())
  {
    selection = core.GetDefaultResourcePackSelection();
    selection.Primary =
        bootstrap.NormalizeEnabledPackIds(core, selection.Primary);
    selection.Secondary =
        bootstrap.NormalizeEnabledPackIds(core, selection.Secondary);
  }
  if (!selection.Primary.empty() &&
      std::find(selection.Primary.begin(), selection.Primary.end(),
                selection.WorldgenOwner) == selection.Primary.end())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  return selection;
}

} // namespace cutum
