#ifndef GUI_ICON_SOURCE_H
#define GUI_ICON_SOURCE_H

#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/PrefabIconCache.h"
#include <memory>

namespace cutum {

class TextureCubeStorage;

class GuiIconSource : public IGuiIconSource {
public:
    GuiIconSource(std::shared_ptr<TextureCubeStorage> textures,
                  std::unique_ptr<PrefabIconCache> prefabCache);

    GLuint GetBlockIconTexture(const std::string& blockName) override;
    GLuint GetPrefabIconTexture(const std::string& prefabName) override;
    GLuint GetPrefabIconTextureIfCached(const std::string& prefabName) const override;

    PrefabIconCache& GetPrefabCache() { return *prefabCache_; }
    void WarmupPrefabIcons(size_t maxPerFrame);

private:
    std::shared_ptr<TextureCubeStorage> textures_;
    std::unique_ptr<PrefabIconCache> prefabCache_;
};

} // namespace cutum

#endif
