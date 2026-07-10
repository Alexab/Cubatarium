#ifndef CHUNKLIGHTING_H
#define CHUNKLIGHTING_H

#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

constexpr int kRelightFrontierIterationsFull = 4;
constexpr int kRelightFrontierIterationsEdit = 2;

struct RelightFrontierOutcome
{
  std::vector<glm::ivec3> relit_chunks;
  bool frontier_unfinished{false};
};

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light = true);
void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos, int max_world_y);
void RelightBlocksAroundLocal(UBlockWorld &world, UBlockRegistry &registry,
                              const std::vector<glm::ivec3> &block_positions);
std::vector<glm::ivec3>
RelightBlocksAroundAll(UBlockWorld &world, UBlockRegistry &registry,
                       const std::vector<glm::ivec3> &block_positions,
                       int max_world_y);
RelightFrontierOutcome RelightBlocksAroundAllEx(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions, int min_world_y,
    int max_world_y, bool include_block_light, int frontier_iterations);
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
