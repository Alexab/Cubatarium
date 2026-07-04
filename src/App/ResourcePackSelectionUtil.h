#ifndef RESOURCEPACKSELECTIONUTIL_H
#define RESOURCEPACKSELECTIONUTIL_H

#include "ResourcePacks/ResourcePack.h"
#include "ResourcePacks/ResourcePackResolver.h"

namespace cutum
{

class UCore;
class UResourcePackBootstrap;

ResourcePackSelection NormalizeResourcePackSelection(
    const UCore &core, const UResourcePackBootstrap &bootstrap,
    const ResourcePackSelection &requested);

} // namespace cutum

#endif // RESOURCEPACKSELECTIONUTIL_H
