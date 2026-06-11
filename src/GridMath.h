#ifndef GRIDMATH_H
#define GRIDMATH_H

#include <glm/glm.hpp>
#include <cmath>

namespace cutum {

int FloorDiv(int a, int b);
int PositiveMod(int a, int b);

glm::vec3 BlockCenter(const glm::ivec3& blockPos);
/// World Y of the top face of a 1×1 block whose center is at integer `blockY`.
inline float BlockTopY(int blockY) { return static_cast<float>(blockY) + 0.5f; }
/// Initial upward speed for a jump that reaches `heightBlocks` under constant gravity magnitude.
inline float JumpSpeedFromHeight(float heightBlocks, float gravityMagnitude = 20.0f)
{
 const float h = heightBlocks > 0.0f ? heightBlocks : 0.0f;
 return std::sqrt(2.0f * gravityMagnitude * h);
}
glm::ivec3 WorldPosToBlock(const glm::vec3& worldPos);
/// Block grid index for a horizontal world coordinate (matches WorldPosToBlock x/z).
inline int WorldCoordToBlockIndex(float coord) { return static_cast<int>(std::floor(coord + 0.5f)); }

extern const glm::ivec3 NEIGHBOR_OFFSETS[6];

}

#endif
