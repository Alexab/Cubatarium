#ifndef GUI_ICON_SOURCE_H
#define GUI_ICON_SOURCE_H

#include "Gui/Cache/CreatureIconCache.h"
#include "Gui/Cache/ObjectIconCache.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include <memory>

namespace cutum
{

class UTextureCubeStorage;

class UGuiIconSource : public IUGuiIconSource
{
public:
  UGuiIconSource(std::shared_ptr<UTextureCubeStorage> textures,
                 std::unique_ptr<UObjectIconCache> objectCache,
                 std::unique_ptr<UCreatureIconCache> creatureCache = nullptr);

  GLuint GetBlockIconTexture(const std::string &blockName) override;
  GLuint GetObjectIconTexture(const std::string &objectName) override;
  GLuint
  GetObjectIconTextureIfCached(const std::string &objectName) const override;
  GLuint GetCreatureIconTexture(const std::string &speciesId) override;
  GLuint GetSkinIconTexture(const std::string &skinId) override;

  UObjectIconCache &GetObjectCache() { return *ObjectCache; }
  void ClearBlockIconCache();
  void ClearCreatureIconCache();
  void WarmupObjectIcons(size_t maxPerFrame);
  void WarmupCreatureIcons(size_t maxPerFrame);

private:
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::unique_ptr<UObjectIconCache> ObjectCache;
  std::unique_ptr<UCreatureIconCache> CreatureCache;
};

} // namespace cutum

#endif
