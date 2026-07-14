#include "Creatures/Core/CreatureCatalogTypes.h"
#include <iostream>

namespace cutum
{

namespace
{

void WarnLegacyBackendOnce(const std::string &legacy, const std::string &canonical)
{
  static bool warned = false;
  if (!warned)
  {
    warned = true;
    std::cerr << "ParseCreatureVisualBackend: legacy backend '" << legacy
              << "' is deprecated; use '" << canonical << "'" << std::endl;
  }
}

} // namespace

CreatureVisualBackend ParseCreatureVisualBackend(const std::string &s)
{
  if (s == "rigid_voxels")
  {
    return CreatureVisualBackend::RigidVoxels;
  }
  if (s == "bone_skeleton")
  {
    return CreatureVisualBackend::BoneSkeleton;
  }
  if (s == "skeletal_geo" || s == "bedrock_geo" || s == "skeletal")
  {
    WarnLegacyBackendOnce(s, "bone_skeleton");
    return CreatureVisualBackend::BoneSkeleton;
  }
  if (s == "gltf_skeleton")
  {
    return CreatureVisualBackend::GltfSkeleton;
  }
  if (!s.empty())
  {
    std::cerr << "ParseCreatureVisualBackend: unknown '" << s
              << "', using rigid_voxels" << std::endl;
  }
  return CreatureVisualBackend::RigidVoxels;
}

const char *ToString(CreatureVisualBackend backend)
{
  switch (backend)
  {
  case CreatureVisualBackend::RigidVoxels:
    return "rigid_voxels";
  case CreatureVisualBackend::BoneSkeleton:
    return "bone_skeleton";
  case CreatureVisualBackend::GltfSkeleton:
    return "gltf_skeleton";
  }
  return "rigid_voxels";
}

std::string CreatureVisualBackendToString(CreatureVisualBackend backend)
{
  return ToString(backend);
}

CreatureTextureLayout ParseCreatureTextureLayout(const std::string &s)
{
  if (s == "player_skin_atlas")
  {
    return CreatureTextureLayout::PlayerSkinAtlas;
  }
  if (s == "rigid_crop" || s.empty())
  {
    return CreatureTextureLayout::RigidCrop;
  }
  std::cerr << "ParseCreatureTextureLayout: unknown '" << s
            << "', using rigid_crop" << std::endl;
  return CreatureTextureLayout::RigidCrop;
}

const char *ToString(CreatureTextureLayout layout)
{
  switch (layout)
  {
  case CreatureTextureLayout::PlayerSkinAtlas:
    return "player_skin_atlas";
  case CreatureTextureLayout::RigidCrop:
    return "rigid_crop";
  }
  return "rigid_crop";
}

} // namespace cutum
