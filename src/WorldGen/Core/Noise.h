#pragma once

#include <array>
#include <cstdint>

namespace cutum
{

float HashNoise2D(int x, int z, uint32_t Seed);
int LegacyHeightAt(int x, int z, uint32_t Seed, int baseY, int MaxHeight);

float Noise2D(int x, int z, uint32_t Seed);
int HeightAt(int x, int z, uint32_t Seed, int baseY, int MaxHeight);

float Perlin2D(float x, float z, uint32_t Seed);
float Perlin3D(float x, float y, float z, uint32_t Seed);

float FBM2D(float x, float z, uint32_t Seed, int octaves, float persistence,
            float lacunarity);
float FBM3D(float x, float y, float z, uint32_t Seed, int octaves,
            float persistence, float lacunarity);

float NormalizedFBM2D(float x, float z, uint32_t Seed, int octaves,
                      float persistence, float lacunarity);
float Smoothstep(float edge0, float edge1, float x);
float TriangularYFactor(int y, int yMin, int yPeak, int yMax);

/// Builds permutation without the thread-local cache (parity tests).
std::array<int, 512> BuildNoisePermutationForSeed(uint32_t seed);

} // namespace cutum
