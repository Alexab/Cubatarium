#pragma once

#include <cstdint>

namespace cutum {

float Noise2D(int x, int z, uint32_t seed);
int HeightAt(int x, int z, uint32_t seed, int baseY, int maxHeight);

}
