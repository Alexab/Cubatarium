#ifndef CREATURERENDERSTATS_H
#define CREATURERENDERSTATS_H

#include <cstdint>

namespace cutum
{

struct CreatureRenderStats
{
  uint32_t CreaturesConsidered{0};
  uint32_t CreaturesDrawn{0};
  uint32_t CreaturesCulled{0};
  uint32_t CreatureDrawCalls{0};
  uint32_t CreatureBoneMatrixUploads{0};

  void Reset()
  {
    CreaturesConsidered = 0;
    CreaturesDrawn = 0;
    CreaturesCulled = 0;
    CreatureDrawCalls = 0;
    CreatureBoneMatrixUploads = 0;
  }
};

} // namespace cutum

#endif
