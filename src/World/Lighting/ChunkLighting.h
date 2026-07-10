#ifndef CHUNKLIGHTING_H
#define CHUNKLIGHTING_H

#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light = true);
void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos, int max_world_y);
std::vector<glm::ivec3>
RelightBlocksAroundAll(UBlockWorld &world, UBlockRegistry &registry,
                       const std::vector<glm::ivec3> &block_positions,
                       int max_world_y);
void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y,
                   bool include_block_light = true);
void RelightColumnWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               bool include_block_light,
                               std::vector<glm::ivec3> *out_relit_chunks);
void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry);

} // namespace cutum

#endif // CHUNKLIGHTING_H
