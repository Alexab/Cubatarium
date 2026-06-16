#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace cutum
{

namespace
{

uint32_t Hash2D(int x, int z, uint32_t Seed)
{
  uint32_t h = static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ Seed;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h;
}

void BuildPermutation(uint32_t Seed, std::array<int, 512> &perm)
{
  std::array<int, 256> p{};
  for (int i = 0; i < 256; ++i)
  {
    p[i] = i;
  }
  uint32_t state = Seed * 747796405u + 2891336453u;
  for (int i = 255; i > 0; --i)
  {
    state = state * 1664525u + 1013904223u;
    const int j = static_cast<int>(state % static_cast<uint32_t>(i + 1));
    std::swap(p[i], p[j]);
  }
  for (int i = 0; i < 512; ++i)
  {
    perm[i] = p[i & 255];
  }
}

float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Lerp(float a, float b, float t) { return a + t * (b - a); }

float Grad2D(int hash, float x, float z)
{
  switch (hash & 3)
  {
  case 0:
    return x + z;
  case 1:
    return -x + z;
  case 2:
    return x - z;
  default:
    return -x - z;
  }
}

float Grad3D(int hash, float x, float y, float z)
{
  const int h = hash & 15;
  const float u = h < 8 ? x : y;
  const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float SamplePerlin2D(float x, float z, const std::array<int, 512> &perm)
{
  const int xi = static_cast<int>(std::floor(x)) & 255;
  const int zi = static_cast<int>(std::floor(z)) & 255;
  const float xf = x - std::floor(x);
  const float zf = z - std::floor(z);

  const float u = Fade(xf);
  const float v = Fade(zf);

  const int aa = perm[xi] + zi;
  const int ab = perm[xi] + zi + 1;
  const int ba = perm[xi + 1] + zi;
  const int bb = perm[xi + 1] + zi + 1;

  const float x1 =
      Lerp(Grad2D(perm[aa], xf, zf), Grad2D(perm[ba], xf - 1.0f, zf), u);
  const float x2 = Lerp(Grad2D(perm[ab], xf, zf - 1.0f),
                        Grad2D(perm[bb], xf - 1.0f, zf - 1.0f), u);
  return Lerp(x1, x2, v);
}

float SamplePerlin3D(float x, float y, float z,
                     const std::array<int, 512> &perm)
{
  const int xi = static_cast<int>(std::floor(x)) & 255;
  const int yi = static_cast<int>(std::floor(y)) & 255;
  const int zi = static_cast<int>(std::floor(z)) & 255;
  const float xf = x - std::floor(x);
  const float yf = y - std::floor(y);
  const float zf = z - std::floor(z);

  const float u = Fade(xf);
  const float v = Fade(yf);
  const float w = Fade(zf);

  const int aaa = perm[perm[xi] + yi] + zi;
  const int aba = perm[perm[xi + 1] + yi] + zi;
  const int aab = perm[perm[xi] + yi + 1] + zi;
  const int abb = perm[perm[xi + 1] + yi + 1] + zi;
  const int baa = perm[perm[xi] + yi] + zi + 1;
  const int bab = perm[perm[xi + 1] + yi] + zi + 1;
  const int bab2 = perm[perm[xi] + yi + 1] + zi + 1;
  const int bbb = perm[perm[xi + 1] + yi + 1] + zi + 1;

  const float x1 = Lerp(Grad3D(perm[aaa], xf, yf, zf),
                        Grad3D(perm[aba], xf - 1.0f, yf, zf), u);
  const float x2 = Lerp(Grad3D(perm[aab], xf, yf - 1.0f, zf),
                        Grad3D(perm[abb], xf - 1.0f, yf - 1.0f, zf), u);
  const float y1 = Lerp(x1, x2, v);

  const float x3 = Lerp(Grad3D(perm[baa], xf, yf, zf - 1.0f),
                        Grad3D(perm[bab], xf - 1.0f, yf, zf - 1.0f), u);
  const float x4 = Lerp(Grad3D(perm[bab2], xf, yf - 1.0f, zf - 1.0f),
                        Grad3D(perm[bbb], xf - 1.0f, yf - 1.0f, zf - 1.0f), u);
  const float y2 = Lerp(x3, x4, v);

  return Lerp(y1, y2, w);
}

std::array<int, 512> PermutationForSeed(uint32_t Seed)
{
  std::array<int, 512> perm{};
  BuildPermutation(Seed, perm);
  return perm;
}

} // namespace

float HashNoise2D(int x, int z, uint32_t Seed)
{
  return Hash2D(x, z, Seed) / 4294967295.0f;
}

int LegacyHeightAt(int x, int z, uint32_t Seed, int baseY, int MaxHeight)
{
  const float n = HashNoise2D(x, z, Seed);
  return baseY + static_cast<int>(n * MaxHeight);
}

float Noise2D(int x, int z, uint32_t Seed) { return HashNoise2D(x, z, Seed); }

int HeightAt(int x, int z, uint32_t Seed, int baseY, int MaxHeight)
{
  return LegacyHeightAt(x, z, Seed, baseY, MaxHeight);
}

float Perlin2D(float x, float z, uint32_t Seed)
{
  const auto perm = PermutationForSeed(Seed);
  return SamplePerlin2D(x, z, perm);
}

float Perlin3D(float x, float y, float z, uint32_t Seed)
{
  const auto perm = PermutationForSeed(Seed);
  return SamplePerlin3D(x, y, z, perm);
}

float FBM2D(float x, float z, uint32_t Seed, int octaves, float persistence,
            float lacunarity)
{
  const auto perm = PermutationForSeed(Seed);
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (int i = 0; i < octaves; ++i)
  {
    value += SamplePerlin2D(x * frequency, z * frequency, perm) * amplitude;
    amplitude *= persistence;
    frequency *= lacunarity;
  }
  return value;
}

float FBM3D(float x, float y, float z, uint32_t Seed, int octaves,
            float persistence, float lacunarity)
{
  const auto perm = PermutationForSeed(Seed);
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (int i = 0; i < octaves; ++i)
  {
    value += SamplePerlin3D(x * frequency, y * frequency, z * frequency, perm) *
             amplitude;
    amplitude *= persistence;
    frequency *= lacunarity;
  }
  return value;
}

} // namespace cutum
