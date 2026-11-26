#include "Gui/Cache/CreatureIconCache.h"
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
    return false;
  RebuildWarmupList();
  return true;
}

void UCreatureIconCache::RebuildWarmupList()
{
  UGuiIconWarmupQueue next;
  if (Preview)
  {
    if (const auto *species = Preview->GetSpecies())
    {
      for (const auto &id : species->ListSpawnable())
        next.Push(id);
      for (const auto &id : species->ListAllIds())
        if (const auto *def = species->Get(id); def && def->role == CreatureRole::ControlledDefault)
          next.Push(id);
    }
    if (const auto *skins = Preview->GetSkins())
      for (const auto &id : skins->ListEquippable())
        next.Push("skin:" + id);
  }
  Warmup.Swap(next);
}

void UCreatureIconCache::ClearRenderedIcons()
{
  RebuildWarmupList();
  SpeciesCache.clear();
  SkinCache.clear();
  if (IconService)
  {
    IconService->InvalidateKind("creature");
    IconService->InvalidateKind("skin");
  }
}

void UCreatureIconCache::Shutdown()
{
  SpeciesCache.clear();
  SkinCache.clear();
  Warmup.Clear();
}

CreatureIconCacheStorage UCreatureIconCache::InspectOwnedStorage() const noexcept
{
  CreatureIconCacheStorage out;
  out.present = true;
  out.cacheBytes = sizeof(*this);
  out.species = InspectIconTextureTable(SpeciesCache, kIconSize * kIconSize);
  out.skins = InspectIconTextureTable(SkinCache, kIconSize * kIconSize);
  const auto queue = Warmup.InspectOwnedStorage();
  out.queueBytes = queue.requested;
  out.pending = Warmup.Size();
  StlSourceStorage total;
  for (size_t bytes : {out.cacheBytes, out.species.cpuBytes, out.skins.cpuBytes, out.queueBytes})
    total.Add(bytes);
  out.cpuBytes = total.bytes.requested;
  out.supported = out.species.supported && out.skins.supported && queue.supported && total.bytes.supported;
  return out;
}

GLuint UCreatureIconCache::GetOrCreateSpeciesIcon(const std::string &speciesId)
{
  const auto it = SpeciesCache.find(speciesId);
  if (it != SpeciesCache.end())
  {
    return it->second.Get();
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
      return SpeciesCache.try_emplace(speciesId, UGlTextureOwner(cachedTex)).first->second.Get();
    }
  }

  // Prefer rendered icon from current 3D model so slot preview stays aligned
  // with the actual creature visual; use packed icon texture only as fallback.
  UGlTextureOwner texture(Preview->RenderToUniqueTexture(speciesId, "", kIconSize,
                                                         kIconYaw, kIconPitch));
  const bool rendered = texture.Get() != 0;
  if (!rendered)
  {
    if (const GLuint direct = Preview->TryGetDirectSpeciesIcon(speciesId))
    {
      return SpeciesCache.try_emplace(speciesId, UGlTextureOwner::Borrow(direct)).first->second.Get();
    }
  }
  if (!rendered)
  {
    glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
    if (UCreatureDefinitionStorage *species = Preview->GetSpecies())
    {
      if (const CreatureDefinition *def = species->Get(speciesId))
      {
        color = def->visual.wireframeColor;
      }
    }
    texture = UGlTextureOwner(Preview->CreateSolidColorTexture(kIconSize, color.r, color.g, color.b, color.a));
  }
  if (!texture.Get())
    return 0;
  const auto tex = SpeciesCache.try_emplace(speciesId, std::move(texture)).first->second.Get();
  if (rendered && IconService && !fingerprint.empty())
  {
    IconService->StoreIconTexture("creature", speciesId, "", fingerprint,
                                  kIconSize, tex);
  }
  return tex;
}

GLuint UCreatureIconCache::GetOrCreateSkinIcon(const std::string &skinId)
{
  const auto it = SkinCache.find(skinId);
  if (it != SkinCache.end())
  {
    return it->second.Get();
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
      return SkinCache.try_emplace(skinId, UGlTextureOwner(cachedTex)).first->second.Get();
    }
  }

  UGlTextureOwner rendered;
  if (Preview->GetTextures())
  {
    if (USkinDefinitionStorage *skins = Preview->GetSkins())
    {
      if (const SkinDefinition *def = skins->Get(skinId))
      {
        rendered = UGlTextureOwner(Preview->RenderToUniqueTexture(def->creatureId, skinId,
                                                                   kIconSize, kIconYaw, kIconPitch));
      }
    }
  }
  if (rendered.Get())
  {
    const auto tex = SkinCache.try_emplace(skinId, std::move(rendered)).first->second.Get();
    if (IconService && !fingerprint.empty())
    {
      IconService->StoreIconTexture("skin", skinId, "", fingerprint, kIconSize,
                                    tex);
    }
    return tex;
  }

  if (UCreatureTextureStorage *textures = Preview->GetTextures())
  {
    const GLuint existing = textures->GetTexture("skin/" + skinId + "/diffuse");
    if (existing != 0)
    {
      return SkinCache.try_emplace(skinId, UGlTextureOwner::Borrow(existing)).first->second.Get();
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
  UGlTextureOwner texture(Preview->CreateSolidColorTexture(kIconSize, color.r, color.g, color.b, color.a));
  if (!texture.Get())
    return 0;
  return SkinCache.try_emplace(skinId, std::move(texture)).first->second.Get();
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
      << def->visual.GetParts().size();
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
    if (!Warmup.RunFront([this](const std::string &key) {
          if (key.rfind("skin:", 0) == 0)
            GetOrCreateSkinIcon(key.substr(5));
          else
            GetOrCreateSpeciesIcon(key);
        }))
      break;
  }
  if (IconService)
  {
    IconService->FlushManifest();
  }
}

} // namespace cutum
