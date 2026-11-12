#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/GuiOffscreenIconCacheBase.h"
#include "Gui/Cache/InventoryIconService.h"

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "App/Platform/IUPlatformPaths.h"
#include "App/CreatureQaAssetRoot.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"

#include "Render/GlIncludes.h"
#include <filesystem>
#include <glm/vec4.hpp>
#include <sstream>
#include <algorithm>
#include <vector>

namespace cutum
{
namespace
{
void AppendAssetFingerprint(std::ostringstream &out,
                            const std::filesystem::path &directory)
{
  // Called only for cache misses/warmup, never for an already cached icon.
  // Geometry and PNG changes must invalidate B/R icons as well as glTF ones.
  out << '|' << directory.generic_string();
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (std::filesystem::recursive_directory_iterator it(directory, ec), end;
       !ec && it != end; it.increment(ec))
  {
    if (it->is_regular_file(ec)) { files.push_back(it->path()); }
  }
  std::sort(files.begin(), files.end());
  for (const auto &file : files)
  {
    out << '|' << file.lexically_relative(directory).generic_string() << ':'
        << std::filesystem::file_size(file, ec) << ':'
        << std::filesystem::last_write_time(file, ec).time_since_epoch().count();
  }
}
} // namespace

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
    Warmup.Clear();
    if (species)
    {
      for (const std::string &Id : species->ListSpawnable())
      {
        Warmup.Push(Id);
      }
      for (const std::string &Id : species->ListAllIds())
      {
        if (const CreatureDefinition *def = species->Get(Id))
        {
          if (def->role == CreatureRole::ControlledDefault)
          {
            Warmup.Push(Id);
          }
        }
      }
    }
    if (skins)
    {
      for (const std::string &Id : skins->ListEquippable())
      {
        Warmup.Push("skin:" + Id);
      }
    }
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
  if (!Preview)
  {
    return;
  }
  UCreatureDefinitionStorage *species = Preview->GetSpecies();
  USkinDefinitionStorage *skins = Preview->GetSkins();
    Warmup.Clear();
    if (species)
    {
      for (const std::string &Id : species->ListSpawnable())
      {
        Warmup.Push(Id);
      }
      for (const std::string &Id : species->ListAllIds())
      {
        if (const CreatureDefinition *def = species->Get(Id))
        {
          if (def->role == CreatureRole::ControlledDefault)
          {
            Warmup.Push(Id);
          }
        }
      }
    }
    if (skins)
    {
      for (const std::string &Id : skins->ListEquippable())
      {
        Warmup.Push("skin:" + Id);
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
  out << "v3|species|" << speciesId << '|' << def->visual.backend << '|'
      << def->visual.textureLayout << '|' << def->visual.iconMode << '|'
      << def->visual.defaultTextureKey << '|'
      << def->visual.boneSkeleton.geometryFile << '|'
      << def->visual.boneSkeleton.geometryId << '|'
      << def->visual.gltf.modelPath << '|' << def->visual.gltf.modelScale
      << '|' << def->visual.gltf.modelOffsetY << '|'
      << def->visual.Parts.size();
  if (const auto *paths = IUPlatformPaths::TryGet())
  {
    AppendAssetFingerprint(out, CreatureModelsRoot(paths->AssetRoot()) /
                                  "creatures" / speciesId);
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
  out << "v3|skin|" << skinId << '|' << def->creatureId << '|'
      << def->textureKey << '|' << def->iconFallbackColor.r << ','
      << def->iconFallbackColor.g << ',' << def->iconFallbackColor.b << ','
      << def->iconFallbackColor.a;
  out << '|' << BuildSpeciesFingerprint(def->creatureId);
  if (const auto *paths = IUPlatformPaths::TryGet())
  {
    AppendAssetFingerprint(out, CreatureModelsRoot(paths->AssetRoot()) /
                                  "skins" / skinId);
  }
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
  for (size_t i = 0; i < count; ++i)
  {
    const auto next = Warmup.PopFront();
    if (!next)
    {
      break;
    }
    const std::string &key = *next;
    if (key.rfind("skin:", 0) == 0)
    {
      GetOrCreateSkinIcon(key.substr(5));
    }
    else
    {
      GetOrCreateSpeciesIcon(key);
    }
  }
  if (IconService)
  {
    IconService->FlushManifest();
  }
}

} // namespace cutum
