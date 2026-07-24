#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/GuiOffscreenIconCacheBase.h"
#include "Gui/Cache/InventoryIconService.h"

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"

#include "Render/GlIncludes.h"
#include <filesystem>
#include <glm/vec4.hpp>
#include <sstream>

namespace cutum
{

UCreatureIconCache::UCreatureIconCache(
    std::shared_ptr<UCreaturePreviewRenderer> preview,
    std::shared_ptr<UInventoryIconService> iconService)
    : Preview(std::move(preview)), IconService(std::move(iconService))
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
  if (IconService)
  {
    IconService->InvalidateKind("creature");
    IconService->InvalidateKind("skin");
  }
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

  const std::string fingerprint = BuildSpeciesFingerprint(speciesId);
  if (IconService && !fingerprint.empty())
  {
    GLuint cachedTex = 0;
    if (IconService->TryLoadIconTexture("creature", speciesId, "", fingerprint,
                                        kIconSize, cachedTex))
    {
      SpeciesCache[speciesId] = cachedTex;
      return cachedTex;
    }
  }

  // Prefer rendered icon from current 3D model so slot preview stays aligned
  // with the actual creature visual; use packed icon texture only as fallback.
  GLuint tex = Preview->RenderToUniqueTexture(speciesId, "", kIconSize,
                                              kIconYaw, kIconPitch);
  const bool rendered = tex != 0;
  if (tex == 0)
  {
    if (const GLuint direct = Preview->TryGetDirectSpeciesIcon(speciesId))
    {
      SpeciesCache[speciesId] = direct;
      return direct;
    }
  }
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
  if (rendered && IconService && !fingerprint.empty())
  {
    IconService->StoreIconTexture("creature", speciesId, "", fingerprint,
                                  kIconSize, tex);
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

  const std::string fingerprint = BuildSkinFingerprint(skinId);
  if (IconService && !fingerprint.empty())
  {
    GLuint cachedTex = 0;
    if (IconService->TryLoadIconTexture("skin", skinId, "", fingerprint,
                                        kIconSize, cachedTex))
    {
      SkinCache[skinId] = cachedTex;
      return cachedTex;
    }
  }

  GLuint rendered = 0;
  if (UCreatureTextureStorage *textures = Preview->GetTextures())
  {
    if (USkinDefinitionStorage *skins = Preview->GetSkins())
    {
      if (const SkinDefinition *def = skins->Get(skinId))
      {
        rendered = Preview->RenderToUniqueTexture(def->creatureId, skinId,
                                                  kIconSize, kIconYaw,
                                                  kIconPitch);
      }
    }
  }
  if (rendered != 0)
  {
    if (IconService && !fingerprint.empty())
    {
      IconService->StoreIconTexture("skin", skinId, "", fingerprint, kIconSize,
                                    rendered);
    }
    SkinCache[skinId] = rendered;
    return rendered;
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

std::string UCreatureIconCache::BuildSpeciesFingerprint(
    const std::string &speciesId) const
{
  if (!Preview || speciesId.empty())
  {
    return {};
  }
  UCreatureDefinitionStorage *species = Preview->GetSpecies();
  if (!species)
  {
    return {};
  }
  const CreatureDefinition *def = species->Get(speciesId);
  if (!def)
  {
    return {};
  }
  std::ostringstream out;
  out << "v2|species|" << speciesId << '|' << def->visual.backend << '|'
      << def->visual.textureLayout << '|' << def->visual.iconMode << '|'
      << def->visual.defaultTextureKey << '|'
      << def->visual.boneSkeleton.geometryFile << '|'
      << def->visual.boneSkeleton.geometryId << '|'
      << def->visual.gltf.modelPath << '|' << def->visual.gltf.modelScale
      << '|' << def->visual.gltf.modelOffsetY << '|'
      << def->visual.Parts.size();
  if (def->visual.backend == "gltf_skeleton")
  {
    if (const auto *paths = IUPlatformPaths::TryGet())
    {
      const std::string modelFile = def->visual.gltf.modelPath.empty()
                                        ? "model.gltf"
                                        : def->visual.gltf.modelPath;
      const std::filesystem::path binPath =
          paths->AssetRoot() / "models" / "creatures" / speciesId /
          std::filesystem::path(modelFile).replace_extension(".bin");
      std::error_code ec;
      if (std::filesystem::exists(binPath, ec))
      {
        out << '|' << std::filesystem::file_size(binPath, ec) << '|'
            << static_cast<long long>(
                   std::filesystem::last_write_time(binPath, ec)
                       .time_since_epoch()
                       .count());
      }
    }
  }
  return out.str();
}

std::string UCreatureIconCache::BuildSkinFingerprint(
    const std::string &skinId) const
{
  if (!Preview || skinId.empty())
  {
    return {};
  }
  USkinDefinitionStorage *skins = Preview->GetSkins();
  if (!skins)
  {
    return {};
  }
  const SkinDefinition *def = skins->Get(skinId);
  if (!def)
  {
    return {};
  }
  std::ostringstream out;
  out << "v2|skin|" << skinId << '|' << def->creatureId << '|'
      << def->textureKey << '|' << def->iconFallbackColor.r << ','
      << def->iconFallbackColor.g << ',' << def->iconFallbackColor.b << ','
      << def->iconFallbackColor.a;
  return out.str();
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
