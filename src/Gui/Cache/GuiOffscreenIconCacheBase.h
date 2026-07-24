#ifndef GUIOFFSCREENICONCACHEBASE_H
#define GUIOFFSCREENICONCACHEBASE_H

#include "Render/GlIncludes.h"
#include <unordered_map>

namespace cutum
{

/// Shared GL teardown for offscreen-rendered icon texture caches.
class UGuiOffscreenIconCacheBase
{
public:
  template <typename Key>
  static void DeleteGlTextures(std::unordered_map<Key, GLuint> &cache,
                               GLuint skipTexture = 0)
  {
    for (auto &entry : cache)
    {
      GLuint tex = entry.second;
      if (tex == 0 || tex == skipTexture)
      {
        continue;
      }
      glDeleteTextures(1, &tex);
    }
    cache.clear();
  }
};

} // namespace cutum

#endif
