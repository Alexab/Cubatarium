#include "Noise.h"

namespace cutum {

namespace {

uint32_t Hash2D(int x, int z, uint32_t seed)
{
 uint32_t h = static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ seed;
 h = (h ^ (h >> 13)) * 1274126177u;
 return h;
}

} // namespace

float Noise2D(int x, int z, uint32_t seed)
{
 return Hash2D(x, z, seed) / 4294967295.0f;
}

int HeightAt(int x, int z, uint32_t seed, int baseY, int maxHeight)
{
 const float n = Noise2D(x, z, seed);
 return baseY + static_cast<int>(n * maxHeight);
}

}
