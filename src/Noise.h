#pragma once

#include <cstdint>

namespace cutum {

float HashNoise2D(int x, int z, uint32_t seed);
int LegacyHeightAt(int x, int z, uint32_t seed, int baseY, int maxHeight);

float Noise2D(int x, int z, uint32_t seed);
int HeightAt(int x, int z, uint32_t seed, int baseY, int maxHeight);

float Perlin2D(float x, float z, uint32_t seed);
float Perlin3D(float x, float y, float z, uint32_t seed);

float FBM2D(float x, float z, uint32_t seed,
    int octaves, float persistence, float lacunarity);
float FBM3D(float x, float y, float z, uint32_t seed,
    int octaves, float persistence, float lacunarity);

} // namespace cutum
