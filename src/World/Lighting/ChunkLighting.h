#ifndef CHUNKLIGHTING_H
#define CHUNKLIGHTING_H

#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord);
void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos);
void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y);
void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry);

} // namespace cutum

#endif // CHUNKLIGHTING_H
