#ifndef GUI_ICON_SOURCE_H
#define GUI_ICON_SOURCE_H

#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Cache/PrefabIconCache.h"
#include <memory>

namespace cutum
{

class UTextureCubeStorage;

class UGuiIconSource : public IGuiIconSource
{
public:
  UGuiIconSource(std::shared_ptr<UTextureCubeStorage> textures,
                 std::unique_ptr<UPrefabIconCache> prefabCache,
                 std::unique_ptr<UCreatureIconCache> creatureCache = nullptr);

  GLuint GetBlockIconTexture(const std::string &blockName) override;
  GLuint GetPrefabIconTexture(const std::string &prefabName) override;
  GLuint
  GetPrefabIconTextureIfCached(const std::string &prefabName) const override;
  GLuint GetCreatureIconTexture(const std::string &speciesId) override;
  GLuint GetSkinIconTexture(const std::string &skinId) override;

  UPrefabIconCache &GetPrefabCache() { return *prefabCache_; }
  void WarmupPrefabIcons(size_t maxPerFrame);
  void WarmupCreatureIcons(size_t maxPerFrame);

private:
  std::shared_ptr<UTextureCubeStorage> textures_;
  std::unique_ptr<UPrefabIconCache> prefabCache_;
  std::unique_ptr<UCreatureIconCache> creatureCache_;
};

} // namespace cutum

#endif
