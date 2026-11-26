#include "Gui/Core/GuiIconSource.h"
#include "Gui/Cache/CreatureItemIconStorage.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Gui/Preview/ItemPreviewRenderer.h"
#include "Render/Textures/TextureCube.h"
#include <array>

namespace cutum
{
namespace
{
template<class T> bool SameOwner(const std::shared_ptr<T> &left, const std::shared_ptr<T> &right) noexcept
{
  return left.get() == right.get() && !left.owner_before(right) && !right.owner_before(left);
}
} // namespace

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

GLuint UGuiIconSource::GetItemStateIcon(const InventoryEntryRef &entry)
{
  return ItemCache ? ItemCache->GetIcon(entry) : 0;
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

void UGuiIconSource::WarmupItemIcons(size_t maxPerFrame)
{
  if (ItemCache)
  {
    ItemCache->WarmupNext(maxPerFrame);
  }
}

size_t UGuiIconSource::PendingItemIcons() const
{
  return ItemCache ? ItemCache->PendingWarmupCount() : 0;
}

GLuint
UGuiIconSource::GetBlockIconTextureIfCached(const std::string &blockName) const
{
  return ObjectCache ? ObjectCache->GetBlockIconTextureIfCached(blockName) : 0;
}

void UGuiIconSource::QueueBlockIcon(const std::string &blockName)
{
  if (ObjectCache)
  {
    ObjectCache->QueueBlockIcon(blockName);
  }
}

void UGuiIconSource::WarmupBlockIcons(size_t maxPerFrame)
{
  if (ObjectCache)
  {
    ObjectCache->WarmupBlockIcons(maxPerFrame);
  }
}

size_t UGuiIconSource::PendingBlockIcons() const
{
  return ObjectCache ? ObjectCache->PendingBlockWarmupCount() : 0;
}

ObjectIconStorage UGuiIconSource::InspectObjectIconStorage(const UObjectLibrary *objects,
    const UTextureCubeStorage *textures, const UBlockDefinitionStorage *blocks,
    const UShaderManager *shaders) const noexcept
{
  ObjectIconStorage out;
  if (Textures.get() != textures)
  {
    out.present = bool(ObjectCache);
    return out;
  }
  if (ObjectCache)
    out = ObjectCache->InspectOwnedStorage(objects, textures, blocks, shaders);
  else
  {
    out.atlasFallback = bool(Textures);
    out.supported = out.bindingsMatch = true;
  }
  if (out.supported)
  {
    StlSourceStorage total;
    out.sourceBytes = sizeof(*this);
    total.Add(out.cpuBytes);
    total.Add(out.sourceBytes);
    out.cpuBytes = total.bytes.requested;
    out.supported = total.bytes.supported;
  }
  return out;
}

InventoryIconStorage UGuiIconSource::InspectInventoryIconStorage() const noexcept
{
  const std::array<const std::shared_ptr<UInventoryIconService> *, 3> routes{
      ObjectCache ? &ObjectCache->IconService : nullptr,
      CreatureCache ? &CreatureCache->IconService : nullptr,
      ItemCache ? &ItemCache->IconService : nullptr};
  const std::shared_ptr<UInventoryIconService> *owner = nullptr;
  size_t aliases = 0;
  bool consistent = true;
  for (const auto *route : routes)
  {
    if (!route)
      continue;
    if (!owner)
      owner = route;
    consistent &= SameOwner(*owner, *route);
    aliases += bool(*route);
  }
  InventoryIconStorage out;
  out.present = aliases != 0;
  out.aliases = aliases;
  if (!consistent)
    return out;
  if (out.present)
  {
    out = (*owner)->InspectOwnedStorage();
    out.aliases = aliases;
    out.supported &= out.factoryOwner;
    return out;
  }
  out.bindingsMatch = out.supported = true;
  return out;
}

CreatureItemIconStorage UGuiIconSource::InspectCreatureItemIconStorage(const UGuiIconSource *source,
    const UContentPreviewRenderer *content, const std::shared_ptr<UCreaturePreviewRenderer> &creaturePreview,
    const UCreatureDefinitionStorage *species, const USkinDefinitionStorage *skins,
    const UCreatureTextureStorage *textures, const UItemDefinitionStorage *items,
    const UShaderManager *shaders) noexcept
{
  const auto *creature = source ? source->CreatureCache.get() : nullptr;
  const auto *item = source ? source->ItemCache.get() : nullptr;
  const std::shared_ptr<UItemPreviewRenderer> empty;
  const auto &itemPreview = item ? item->ItemPreview : content ? content->ItemPreview : empty;

  CreatureItemIconStorage out;
  out.bindingsMatch = (!creature || SameOwner(creature->Preview, creaturePreview)) &&
      (!item || item->Items.get() == items) &&
      (!content || (SameOwner(content->Creatures, creaturePreview) && SameOwner(content->ItemPreview, itemPreview)));
  if (!out.bindingsMatch)
    return out;
  if (creature)
    out.creature = creature->InspectOwnedStorage();
  if (item)
    out.item = item->InspectOwnedStorage();
  if (creaturePreview)
  {
    out.creaturePreview = creaturePreview->InspectOwnedStorage(species, skins, textures, shaders);
    out.creaturePreviewAliases = 1 + size_t(creature != nullptr) + size_t(content != nullptr);
  }
  if (itemPreview)
  {
    out.itemPreview = itemPreview->InspectOwnedStorage(items, shaders);
    out.itemPreviewAliases = size_t(item != nullptr) + size_t(content != nullptr);
  }
  StlSourceStorage total;
  for (size_t bytes : {out.creature.cpuBytes, out.item.cpuBytes, out.creaturePreview.cpuBytes, out.itemPreview.cpuBytes})
    total.Add(bytes);
  out.cpuBytes = total.bytes.requested;
  out.supported = total.bytes.supported && (!creature || out.creature.supported) &&
      (!item || out.item.supported) && (!creaturePreview || out.creaturePreview.supported) &&
      (!itemPreview || out.itemPreview.supported);
  return out;
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
