#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/GuiOffscreenIconCacheBase.h"

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"

#include "Render/GlIncludes.h"
#include <glm/vec4.hpp>

namespace cutum
{

UCreatureIconCache::UCreatureIconCache(
    std::shared_ptr<UCreaturePreviewRenderer> preview)
    : Preview(std::move(preview))
{
}

UCreatureIconCache::~UCreatureIconCache() { Shutdown(); }

bool UCreatureIconCache::Initialize()
{
  if (!Preview)
  {
    return false;
  }
  UCreatureDefinitionStorage *species = Preview->GetSpecies();
  USkinDefinitionStorage *skins = Preview->GetSkins();
  if (species)
  {
    WarmupQueue = species->ListSpawnable();
    for (const std::string &Id : species->ListAllIds())
    {
      if (const CreatureDefinition *def = species->Get(Id))
      {
        if (def->role == CreatureRole::ControlledDefault)
        {
          WarmupQueue.push_back(Id);
        }
      }
    }
  }
  if (skins)
  {
    for (const std::string &Id : skins->ListEquippable())
    {
      WarmupQueue.push_back("skin:" + Id);
    }
  }
  WarmupIndex = 0;
  return true;
}

void UCreatureIconCache::ClearRenderedIcons()
{
  UGuiOffscreenIconCacheBase::DeleteGlTextures(SpeciesCache);
  for (const auto &entry : SkinCache)
  {
    GLuint tex = entry.second;
    if (tex == 0)
    {
      continue;
    }
    if (Preview && Preview->GetTextures())
    {
      const GLuint diffuse = Preview->GetTextures()->GetTexture(
          "skin/" + entry.first + "/diffuse");
      if (tex == diffuse)
      {
        continue;
      }
    }
    glDeleteTextures(1, &tex);
  }
  SpeciesCache.clear();
  SkinCache.clear();
  WarmupIndex = 0;
  if (!Preview)
  {
    return;
  }
  UCreatureDefinitionStorage *species = Preview->GetSpecies();
  USkinDefinitionStorage *skins = Preview->GetSkins();
  if (species)
  {
    WarmupQueue = species->ListSpawnable();
    for (const std::string &Id : species->ListAllIds())
    {
      if (const CreatureDefinition *def = species->Get(Id))
      {
        if (def->role == CreatureRole::ControlledDefault)
        {
          WarmupQueue.push_back(Id);
        }
      }
    }
  }
  if (skins)
  {
    for (const std::string &Id : skins->ListEquippable())
    {
      WarmupQueue.push_back("skin:" + Id);
    }
  }
}

void UCreatureIconCache::Shutdown()
{
  UGuiOffscreenIconCacheBase::DeleteGlTextures(SpeciesCache);
  for (const auto &entry : SkinCache)
  {
    if (entry.second == 0)
    {
      continue;
    }
    if (Preview && Preview->GetTextures())
    {
      const GLuint diffuse = Preview->GetTextures()->GetTexture(
          "skin/" + entry.first + "/diffuse");
      if (entry.second == diffuse)
      {
        continue;
      }
    }
    glDeleteTextures(1, &entry.second);
  }
  SpeciesCache.clear();
  SkinCache.clear();
}

GLuint UCreatureIconCache::GetOrCreateSpeciesIcon(const std::string &speciesId)
{
  const auto it = SpeciesCache.find(speciesId);
  if (it != SpeciesCache.end())
  {
    return it->second;
  }
  if (!Preview)
  {
    return 0;
  }

  if (const GLuint direct = Preview->TryGetDirectSpeciesIcon(speciesId))
  {
    SpeciesCache[speciesId] = direct;
    return direct;
  }

  GLuint tex = Preview->RenderToUniqueTexture(speciesId, "", kIconSize,
                                              kIconYaw, kIconPitch);
  if (tex == 0)
  {
    glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
    if (UCreatureDefinitionStorage *species = Preview->GetSpecies())
    {
      if (const CreatureDefinition *def = species->Get(speciesId))
      {
        color = def->visual.wireframeColor;
      }
    }
    tex = Preview->CreateSolidColorTexture(kIconSize, color.r, color.g, color.b,
                                           color.a);
  }
  SpeciesCache[speciesId] = tex;
  return tex;
}

GLuint UCreatureIconCache::GetOrCreateSkinIcon(const std::string &skinId)
{
  const auto it = SkinCache.find(skinId);
  if (it != SkinCache.end())
  {
    return it->second;
  }
  if (!Preview)
  {
    return 0;
  }

  if (UCreatureTextureStorage *textures = Preview->GetTextures())
  {
    const GLuint existing = textures->GetTexture("skin/" + skinId + "/diffuse");
    if (existing != 0)
    {
      SkinCache[skinId] = existing;
      return existing;
    }
  }

  glm::vec4 color{0.7f, 0.7f, 0.7f, 1.0f};
  if (USkinDefinitionStorage *skins = Preview->GetSkins())
  {
    if (const SkinDefinition *def = skins->Get(skinId))
    {
      color = def->iconFallbackColor;
    }
  }
  const GLuint tex = Preview->CreateSolidColorTexture(
      kIconSize, color.r, color.g, color.b, color.a);
  SkinCache[skinId] = tex;
  return tex;
}

GLuint UCreatureIconCache::GetSpeciesIcon(const std::string &speciesId)
{
  if (speciesId.empty())
  {
    return 0;
  }
  return GetOrCreateSpeciesIcon(speciesId);
}

GLuint UCreatureIconCache::GetSkinIcon(const std::string &skinId)
{
  if (skinId.empty())
  {
    return 0;
  }
  return GetOrCreateSkinIcon(skinId);
}

void UCreatureIconCache::WarmupNext(size_t count)
{
  for (size_t i = 0; i < count && WarmupIndex < WarmupQueue.size();
       ++i, ++WarmupIndex)
  {
    const std::string &key = WarmupQueue[WarmupIndex];
    if (key.rfind("skin:", 0) == 0)
    {
      GetOrCreateSkinIcon(key.substr(5));
    }
    else
    {
      GetOrCreateSpeciesIcon(key);
    }
  }
}

} // namespace cutum
