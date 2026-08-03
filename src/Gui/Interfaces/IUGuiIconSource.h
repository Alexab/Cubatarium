#ifndef IU_GUI_ICON_SOURCE_H
#define IU_GUI_ICON_SOURCE_H

#include <string>

typedef unsigned int GLuint;

namespace cutum
{

class IUGuiIconSource
{
public:
  virtual ~IUGuiIconSource() = default;
  virtual GLuint GetBlockIconTexture(const std::string &blockName) = 0;
  virtual GLuint GetObjectIconTexture(const std::string &objectName) = 0;
  /// Без FBO-рендера; 0 если иконка ещё не в кэше.
  virtual GLuint
  GetObjectIconTextureIfCached(const std::string &objectName) const = 0;
  virtual GLuint GetCreatureIconTexture(const std::string &speciesId) = 0;
  virtual GLuint GetSkinIconTexture(const std::string &skinId) = 0;
  virtual GLuint GetItemIconTexture(const std::string &itemId) = 0;
};

} // namespace cutum

#endif
