#ifndef I_GUI_ICON_SOURCE_H
#define I_GUI_ICON_SOURCE_H

#include <string>

typedef unsigned int GLuint;

namespace cutum {

class IGuiIconSource {
public:
    virtual ~IGuiIconSource() = default;
    virtual GLuint GetBlockIconTexture(const std::string& blockName) = 0;
    virtual GLuint GetPrefabIconTexture(const std::string& prefabName) = 0;
    /// Без FBO-рендера; 0 если иконка ещё не в кэше.
    virtual GLuint GetPrefabIconTextureIfCached(const std::string& prefabName) const = 0;
    virtual GLuint GetCreatureIconTexture(const std::string& speciesId) = 0;
    virtual GLuint GetSkinIconTexture(const std::string& skinId) = 0;
};

} // namespace cutum

#endif
