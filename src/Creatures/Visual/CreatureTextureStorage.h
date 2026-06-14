#ifndef CREATURETEXTURESTORAGE_H
#define CREATURETEXTURESTORAGE_H

#include <string>
#include <unordered_map>

#include "Render/GlIncludes.h"

namespace cutum
{

class UCreatureTextureStorage
{
public:
  void LoadFromCreatureAndSkinRoots(const std::string &creaturesRoot,
                                    const std::string &skinsRoot);
  GLuint GetTexture(const std::string &assetKey) const;
  size_t Count() const { return textures_.size(); }

private:
  std::unordered_map<std::string, GLuint> textures_;
};

} // namespace cutum

#endif
