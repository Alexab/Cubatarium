#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace cutum
{
inline constexpr size_t NoisePermutationCacheEntryLimit=2048;
struct NoisePermutationCacheStorage
{
  size_t entries{},buckets{},map_inline{},value_bytes{},node_bytes{},bucket_slots_upper{};
  size_t retained_requests{},retained_upper{},peak_upper{};
  bool supported{},overflow{};
};
// Calling-thread diagnostic after a real noise call. It may initialize the
// same empty map on a cold thread;it adds no cache/history/observer owner.
NoisePermutationCacheStorage GetNoisePermutationCacheStorage();

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
/// Profile-3 terrain noise: Perlin with 8 evenly spaced gradient directions
/// (the legacy 2D noise only has the four diagonals and shows grid-aligned
/// ridges). Normalized to roughly [-1, 1].
float SmoothFBM2D(float x, float z, uint32_t Seed, int octaves,
                  float persistence, float lacunarity);
float Smoothstep(float edge0, float edge1, float x);
float TriangularYFactor(int y, int yMin, int yPeak, int yMax);

/// Builds permutation without the thread-local cache (parity tests).
std::array<int, 512> BuildNoisePermutationForSeed(uint32_t seed);

} // namespace cutum
