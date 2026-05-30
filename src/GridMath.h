#ifndef GRIDMATH_H
#define GRIDMATH_H

#include <glm/glm.hpp>

namespace cutum {

int FloorDiv(int a, int b);
int PositiveMod(int a, int b);

glm::vec3 BlockCenter(const glm::ivec3& blockPos);
glm::ivec3 WorldPosToBlock(const glm::vec3& worldPos);

extern const glm::ivec3 NEIGHBOR_OFFSETS[6];

}

#endif
