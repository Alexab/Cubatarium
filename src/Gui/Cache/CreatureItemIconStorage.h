#pragma once

#include "Gui/Cache/IconCacheStorage.h"
#include "Gui/Preview/PreviewRendererStorage.h"

namespace cutum
{

struct CreatureItemIconStorage
{
  CreatureIconCacheStorage creature;
  ItemIconCacheStorage item;
  PreviewRendererStorage creaturePreview, itemPreview;
  size_t cpuBytes{}, creaturePreviewAliases{}, itemPreviewAliases{};
  bool bindingsMatch{}, supported{};
};

} // namespace cutum
