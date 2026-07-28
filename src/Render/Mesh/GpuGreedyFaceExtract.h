#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Blocks/BlockRegistry.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

// BuildPaddedOccupancy uses std::vector.

namespace cutum
{

/// CPU reference for G5 compute face extract: one unmerged quad per exposed
/// face of solid opaque voxels (no fluids/cutout). Used for parity tests and
/// as decode target for GPU face-mask SSBO.
inline bool IsOpaqueSolidForGpuExtract(UBlockRegistry &registry, BlockId id)
{
  if (id == 0)
  {
    return false;
  }
  if (registry.IsTransparent(id))
  {
    return false;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Cutout ||
      registry.GetRenderStyle(id) == BlockRenderStyle::Fluid)
  {
    return false;
  }
  return registry.IsSolid(id);
}

inline bool SnapshotIsGpuExtractEligible(const ChunkMeshSnapshot &snap,
                                         UBlockRegistry &registry)
{
  for (BlockId id : snap.blocks)
  {
    if (id == 0)
    {
      continue;
    }
    if (!IsOpaqueSolidForGpuExtract(registry, id))
    {
      return false;
    }
  }
  return true;
}

/// Occupancy: 1 = opaque solid, 0 = empty/other. Size CHUNK_VOLUME.
inline void BuildOccupancy(const ChunkMeshSnapshot &snap,
                           UBlockRegistry &registry,
                           std::array<uint8_t, CHUNK_VOLUME> &occ)
{
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    occ[static_cast<size_t>(i)] =
        IsOpaqueSolidForGpuExtract(registry, snap.blocks[static_cast<size_t>(i)])
            ? 1u
            : 0u;
  }
}

/// Padded (CHUNK_SIZE+2)^3 occupancy including one-block shell for GPU extract.
inline constexpr int kGpuOccPad = CHUNK_SIZE + 2;
inline constexpr int kGpuOccPadVolume = kGpuOccPad * kGpuOccPad * kGpuOccPad;

inline void BuildPaddedOccupancy(const ChunkMeshSnapshot &snap,
                                 UBlockRegistry &registry,
                                 std::vector<uint8_t> &occ)
{
  occ.assign(static_cast<size_t>(kGpuOccPadVolume), 0);
  const int pad = kGpuOccPad;
  for (int y = -1; y <= CHUNK_SIZE; ++y)
  {
    for (int z = -1; z <= CHUNK_SIZE; ++z)
    {
      for (int x = -1; x <= CHUNK_SIZE; ++x)
      {
        const glm::ivec3 world = snap.ChunkOrigin() + glm::ivec3(x, y, z);
        const BlockId id = snap.GetBlock(world);
        const int pi = ((y + 1) * pad + (z + 1)) * pad + (x + 1);
        occ[static_cast<size_t>(pi)] =
            IsOpaqueSolidForGpuExtract(registry, id) ? 1u : 0u;
      }
    }
  }
}

/// Padded light: center + one-block shell (parity with FaceLightPacked air sample).
inline void BuildPaddedLight(const ChunkMeshSnapshot &snap,
                             std::vector<uint8_t> &lights)
{
  lights.assign(static_cast<size_t>(kGpuOccPadVolume), 0);
  const int pad = kGpuOccPad;
  for (int y = -1; y <= CHUNK_SIZE; ++y)
  {
    for (int z = -1; z <= CHUNK_SIZE; ++z)
    {
      for (int x = -1; x <= CHUNK_SIZE; ++x)
      {
        const glm::ivec3 world = snap.ChunkOrigin() + glm::ivec3(x, y, z);
        const int pi = ((y + 1) * pad + (z + 1)) * pad + (x + 1);
        lights[static_cast<size_t>(pi)] = snap.GetLightPacked(world);
      }
    }
  }
}

/// Same rule as GreedyMesher::FaceLightPacked: prefer air-neighbor light.
inline uint8_t FaceLightPackedSnap(const ChunkMeshSnapshot &snap,
                                   glm::ivec3 local_solid, int axis, int sign)
{
  glm::ivec3 air = local_solid;
  air[axis] += sign;
  const uint8_t face =
      snap.GetLightPacked(snap.ChunkOrigin() + air);
  if (face != 0)
  {
    return face;
  }
  return snap.GetLightPacked(snap.ChunkOrigin() + local_solid);
}

inline BlockId NeighborBlock(const ChunkMeshSnapshot &snap, int lx, int ly,
                             int lz, int axis, int sign)
{
  glm::ivec3 local(lx, ly, lz);
  local[axis] += sign;
  const glm::ivec3 world = snap.ChunkOrigin() + local;
  return snap.GetBlock(world);
}

/// Emit 1x1 greedy quads for exposed opaque faces (CPU reference / decode).
inline std::vector<GreedyQuad>
ExtractOpaqueFacesCpu(const ChunkMeshSnapshot &snap, UBlockRegistry &registry)
{
  std::vector<GreedyQuad> quads;
  quads.reserve(256);
  const int n = CHUNK_SIZE;
  for (int y = 0; y < n; ++y)
  {
    for (int z = 0; z < n; ++z)
    {
      for (int x = 0; x < n; ++x)
      {
        const int li = (y * n + z) * n + x;
        const BlockId id = snap.blocks[static_cast<size_t>(li)];
        if (!IsOpaqueSolidForGpuExtract(registry, id))
        {
          continue;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
          for (int sign : {-1, 1})
          {
            const BlockId nb = NeighborBlock(snap, x, y, z, axis, sign);
            if (IsOpaqueSolidForGpuExtract(registry, nb))
            {
              continue;
            }
            GreedyQuad q;
            q.axis = axis;
            q.slice = (axis == 0 ? x : (axis == 1 ? y : z)) + (sign > 0 ? 1 : 0);
            q.u = (axis == 0) ? z : x;
            q.v = (axis == 1) ? z : y;
            if (axis == 0)
            {
              q.u = z;
              q.v = y;
            }
            else if (axis == 1)
            {
              q.u = x;
              q.v = z;
            }
            else
            {
              q.u = x;
              q.v = y;
            }
            q.width = 1;
            q.height = 1;
            q.Id = id;
            q.faceSign = sign;
            q.LightPacked =
                FaceLightPackedSnap(snap, glm::ivec3(x, y, z), axis, sign);
            q.FluidPacked = 0;
            quads.push_back(q);
          }
        }
      }
    }
  }
  return quads;
}

/// Strict greedy merge of 1x1 opaque quads (same axis/sign/slice/id/light).
inline std::vector<GreedyQuad>
MergeOpaqueQuadsStrict(const std::vector<GreedyQuad> &input)
{
  struct PlaneKey
  {
    int axis;
    int faceSign;
    int slice;
    BlockId id;
    uint8_t light;
    bool operator==(const PlaneKey &o) const
    {
      return axis == o.axis && faceSign == o.faceSign && slice == o.slice &&
             id == o.id && light == o.light;
    }
  };
  struct PlaneKeyHash
  {
    size_t operator()(const PlaneKey &k) const
    {
      size_t h = static_cast<size_t>(k.axis);
      h = h * 131u + static_cast<size_t>(k.faceSign + 2);
      h = h * 131u + static_cast<size_t>(k.slice + 64);
      h = h * 131u + static_cast<size_t>(k.id);
      h = h * 131u + static_cast<size_t>(k.light);
      return h;
    }
  };

  std::unordered_map<PlaneKey, std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE>,
                     PlaneKeyHash>
      planes;
  for (const GreedyQuad &q : input)
  {
    if (q.width != 1 || q.height != 1)
    {
      continue;
    }
    if (q.u < 0 || q.u >= CHUNK_SIZE || q.v < 0 || q.v >= CHUNK_SIZE)
    {
      continue;
    }
    PlaneKey key{q.axis, q.faceSign, q.slice, q.Id, q.LightPacked};
    auto it = planes.find(key);
    if (it == planes.end())
    {
      std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE> grid{};
      grid[static_cast<size_t>(q.v * CHUNK_SIZE + q.u)] = 1;
      planes.emplace(key, grid);
    }
    else
    {
      it->second[static_cast<size_t>(q.v * CHUNK_SIZE + q.u)] = 1;
    }
  }

  std::vector<GreedyQuad> out;
  out.reserve(input.size());
  for (auto &entry : planes)
  {
    const PlaneKey &key = entry.first;
    auto &grid = entry.second;
    for (int v = 0; v < CHUNK_SIZE; ++v)
    {
      for (int u = 0; u < CHUNK_SIZE; ++u)
      {
        const size_t i = static_cast<size_t>(v * CHUNK_SIZE + u);
        if (!grid[i])
        {
          continue;
        }
        int width = 1;
        while (u + width < CHUNK_SIZE &&
               grid[static_cast<size_t>(v * CHUNK_SIZE + u + width)])
        {
          ++width;
        }
        int height = 1;
        bool grow = true;
        while (grow && v + height < CHUNK_SIZE)
        {
          for (int du = 0; du < width; ++du)
          {
            if (!grid[static_cast<size_t>((v + height) * CHUNK_SIZE + u + du)])
            {
              grow = false;
              break;
            }
          }
          if (grow)
          {
            ++height;
          }
        }
        for (int dv = 0; dv < height; ++dv)
        {
          for (int du = 0; du < width; ++du)
          {
            grid[static_cast<size_t>((v + dv) * CHUNK_SIZE + u + du)] = 0;
          }
        }
        GreedyQuad q;
        q.axis = key.axis;
        q.faceSign = key.faceSign;
        q.slice = key.slice;
        q.u = u;
        q.v = v;
        q.width = width;
        q.height = height;
        q.Id = key.id;
        q.LightPacked = key.light;
        q.FluidPacked = 0;
        out.push_back(q);
      }
    }
  }
  return out;
}

/// Decode GPU face-mask SSBO (6 bits per voxel) into 1x1 quads.
inline std::vector<GreedyQuad>
DecodeFaceMasks(const ChunkMeshSnapshot &snap, UBlockRegistry &registry,
                const std::vector<uint32_t> &masks)
{
  (void)registry;
  std::vector<GreedyQuad> quads;
  const int n = CHUNK_SIZE;
  const size_t vol = static_cast<size_t>(CHUNK_VOLUME);
  for (size_t i = 0; i < vol && i < masks.size(); ++i)
  {
    const uint32_t m = masks[i];
    if (m == 0)
    {
      continue;
    }
    const int x = static_cast<int>(i % n);
    const int y = static_cast<int>(i / (n * n));
    const int z = static_cast<int>((i / n) % n);
    const BlockId id = snap.blocks[i];
    auto emit = [&](int axis, int sign, uint32_t bit)
    {
      if ((m & bit) == 0)
      {
        return;
      }
      GreedyQuad q;
      q.axis = axis;
      q.slice = (axis == 0 ? x : (axis == 1 ? y : z)) + (sign > 0 ? 1 : 0);
      if (axis == 0)
      {
        q.u = z;
        q.v = y;
      }
      else if (axis == 1)
      {
        q.u = x;
        q.v = z;
      }
      else
      {
        q.u = x;
        q.v = y;
      }
      q.width = 1;
      q.height = 1;
      q.Id = id;
      q.faceSign = sign;
      q.LightPacked =
          FaceLightPackedSnap(snap, glm::ivec3(x, y, z), axis, sign);
      q.FluidPacked = 0;
      quads.push_back(q);
    };
    emit(0, -1, 1u);
    emit(0, 1, 2u);
    emit(1, -1, 4u);
    emit(1, 1, 8u);
    emit(2, -1, 16u);
    emit(2, 1, 32u);
  }
  return quads;
}

} // namespace cutum
