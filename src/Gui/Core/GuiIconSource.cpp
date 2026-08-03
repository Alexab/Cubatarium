#include "Gui/Core/GuiIconSource.h"
#include "Render/Textures/TextureCube.h"

namespace cutum
{

UGuiIconSource::UGuiIconSource(
    std::shared_ptr<UTextureCubeStorage> textures,
    std::unique_ptr<UObjectIconCache> objectCache,
    std::unique_ptr<UCreatureIconCache> creatureCache,
    std::unique_ptr<UItemIconCache> itemCache)
    : Textures(std::move(textures)), ObjectCache(std::move(objectCache)),
      CreatureCache(std::move(creatureCache)), ItemCache(std::move(itemCache))
{
}

GLuint UGuiIconSource::GetBlockIconTexture(const std::string &blockName)
{
  if (!ObjectCache)
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
  return ObjectCache->GetBlockIconTexture(blockName);
}

GLuint UGuiIconSource::GetObjectIconTexture(const std::string &objectName)
{
  if (!ObjectCache || objectName.empty())
  {
    return 0;
  }
  return ObjectCache->GetIcon(objectName);
}

GLuint UGuiIconSource::GetObjectIconTextureIfCached(
    const std::string &objectName) const
{
  if (!ObjectCache || objectName.empty())
  {
    return 0;
  }
  return ObjectCache->GetIconIfCached(objectName);
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

GLuint UGuiIconSource::GetItemIconTexture(const std::string &itemId)
{
  if (!ItemCache || itemId.empty())
  {
    return 0;
  }
  return ItemCache->GetIcon(itemId);
}

void UGuiIconSource::WarmupCreatureIcons(size_t maxPerFrame)
{
  if (CreatureCache)
  {
    CreatureCache->WarmupNext(maxPerFrame);
  }
}

void UGuiIconSource::WarmupObjectIcons(size_t maxPerFrame)
{
  if (ObjectCache)
  {
    ObjectCache->WarmupNext(maxPerFrame);
  }
}

void UGuiIconSource::ClearBlockIconCache()
{
  if (ObjectCache)
  {
    ObjectCache->ClearBlockIconCache();
  }
}

void UGuiIconSource::ClearCreatureIconCache()
{
  if (CreatureCache)
  {
    CreatureCache->ClearRenderedIcons();
  }
}

void UGuiIconSource::ClearItemIconCache()
{
  if (ItemCache)
  {
    ItemCache->Invalidate();
  }
}

} // namespace cutum
