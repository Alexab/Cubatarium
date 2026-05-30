#include "GridMath.h"
#include <cmath>

namespace cutum {

const glm::ivec3 NEIGHBOR_OFFSETS[6] = {
    glm::ivec3(1, 0, 0),
    glm::ivec3(-1, 0, 0),
    glm::ivec3(0, 1, 0),
    glm::ivec3(0, -1, 0),
    glm::ivec3(0, 0, 1),
    glm::ivec3(0, 0, -1),
};

int FloorDiv(int a, int b)
{
 if (b == 0) {
  return 0;
 }
 if (a >= 0) {
  return a / b;
 }
 return (a - b + 1) / b;
}

int PositiveMod(int a, int b)
{
 if (b == 0) {
  return 0;
 }
 return ((a % b) + b) % b;
}

glm::vec3 BlockCenter(const glm::ivec3& blockPos)
{
 return glm::vec3(static_cast<float>(blockPos.x),
                  static_cast<float>(blockPos.y),
                  static_cast<float>(blockPos.z));
}

glm::ivec3 WorldPosToBlock(const glm::vec3& worldPos)
{
 return glm::ivec3(
     static_cast<int>(std::round(worldPos.x)),
     static_cast<int>(std::round(worldPos.y)),
     static_cast<int>(std::round(worldPos.z)));
}

}
