#include "Gui/Core/GuiIconSource.h"
#include "Render/Textures/TextureCube.h"

namespace cutum
{

UGuiIconSource::UGuiIconSource(
    std::shared_ptr<UTextureCubeStorage> textures,
    std::unique_ptr<UPrefabIconCache> prefabCache,
    std::unique_ptr<UCreatureIconCache> creatureCache)
    : Textures(std::move(textures)), PrefabCache(std::move(prefabCache)),
      CreatureCache(std::move(creatureCache))
{
}

GLuint UGuiIconSource::GetBlockIconTexture(const std::string &blockName)
{
  if (!PrefabCache)
  {
    if (!Textures || blockName.empty())
    {
      return 0;
    }
    const auto &texMap = Textures->GetTextures();
    for (const auto &kv : texMap)
    {
      if (kv.second.GetName() == blockName)
      {
        return kv.second.GetTexture();
      }
    }
    return 0;
  }
  return PrefabCache->GetBlockIconTexture(blockName);
}

GLuint UGuiIconSource::GetPrefabIconTexture(const std::string &prefabName)
{
  if (!PrefabCache || prefabName.empty())
  {
    return 0;
  }
  return PrefabCache->GetIcon(prefabName);
}

GLuint UGuiIconSource::GetPrefabIconTextureIfCached(
    const std::string &prefabName) const
{
  if (!PrefabCache || prefabName.empty())
  {
    return 0;
  }
  return PrefabCache->GetIconIfCached(prefabName);
}

GLuint UGuiIconSource::GetCreatureIconTexture(const std::string &speciesId)
{
  if (!CreatureCache || speciesId.empty())
  {
    return 0;
  }
  return CreatureCache->GetSpeciesIcon(speciesId);
}

GLuint UGuiIconSource::GetSkinIconTexture(const std::string &skinId)
{
  if (!CreatureCache || skinId.empty())
  {
    return 0;
  }
  return CreatureCache->GetSkinIcon(skinId);
}

void UGuiIconSource::WarmupCreatureIcons(size_t maxPerFrame)
{
  if (CreatureCache)
  {
    CreatureCache->WarmupNext(maxPerFrame);
  }
}

void UGuiIconSource::WarmupPrefabIcons(size_t maxPerFrame)
{
  if (PrefabCache)
  {
    PrefabCache->WarmupNext(maxPerFrame);
  }
}

void UGuiIconSource::ClearBlockIconCache()
{
  if (PrefabCache)
  {
    PrefabCache->ClearBlockIconCache();
  }
}

} // namespace cutum
