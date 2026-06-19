#ifndef CREATUREPACKMERGE_H
#define CREATUREPACKMERGE_H

#include "ResourcePacks/ResourcePack.h"
#include <filesystem>
#include <vector>

namespace cutum
{

class UCreatureDefinitionStorage;
class UCreatureTextureStorage;

void ApplyCreaturePackOverlays(UCreatureDefinitionStorage &defs,
                               UCreatureTextureStorage &textures,
                               const std::filesystem::path &baseCreaturesRoot,
                               const std::filesystem::path &baseSkinsRoot,
                               const std::vector<ResourcePackManifest> &packs);

} // namespace cutum

#endif
