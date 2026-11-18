#pragma once

#include "Blocks/BlockCatalogQueries.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Lighting/LightUtil.h"
#include "World/Mesh/Contracts/CubeFaceVisibility.h"
#include "World/Mesh/Contracts/MeshNeighborPolicy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

// CPU/GPU visibility uploads share full material words and adjacent geometry.

namespace cutum
{

/// PackedQuad has a 10-bit block field. Keep its value as a per-chunk palette
/// index and preserve full 16-bit BlockIds in the side table used by draw
/// ranges. Index zero is reserved for air so the compute grid's zero sentinel
/// remains unambiguous.
inline constexpr size_t kGpuBlockTypePaletteCapacity = 1u << 10u;

inline bool
BuildGpuBlockTypePalette(const ChunkMeshSnapshot &snapshot,
                         std::vector<BlockId> &block_palette,
                         std::vector<uint16_t> &block_palette_indices)
{
  block_palette.clear();
  block_palette_indices.clear();
  block_palette.reserve(kGpuBlockTypePaletteCapacity);

  std::unordered_map<BlockId, uint16_t> indices_by_block;
  indices_by_block.reserve(64);
  block_palette.push_back(BLOCK_AIR);
  for (BlockId id : snapshot.blocks)
  {
    if (id == BLOCK_AIR || indices_by_block.count(id) != 0)
    {
      continue;
    }
    if (block_palette.size() >= kGpuBlockTypePaletteCapacity)
    {
      block_palette.clear();
      return false;
    }
    const uint16_t index = static_cast<uint16_t>(block_palette.size());
    block_palette.push_back(id);
    indices_by_block.emplace(id, index);
  }

  block_palette_indices.resize(snapshot.blocks.size(), 0u);
  for (size_t i = 0; i < snapshot.blocks.size(); ++i)
  {
    const BlockId id = snapshot.blocks[i];
    if (id == BLOCK_AIR)
    {
      continue;
    }
    const auto found = indices_by_block.find(id);
    if (found == indices_by_block.end())
    {
      block_palette.clear();
      block_palette_indices.clear();
      return false;
    }
    block_palette_indices[i] = found->second;
  }
  return true;
}

inline bool
TryResolveGpuBlockTypePaletteIndex(const std::vector<BlockId> &block_palette,
                                   uint32_t palette_index,
                                   BlockId &out_block_id)
{
  if (palette_index == 0 || palette_index >= block_palette.size())
  {
    return false;
  }
  out_block_id = block_palette[palette_index];
  return out_block_id != BLOCK_AIR;
}

inline BlockId
ResolveGpuBlockTypePaletteIndex(const std::vector<BlockId> &block_palette,
                                uint32_t palette_index)
{
  BlockId block_id = BLOCK_AIR;
  return TryResolveGpuBlockTypePaletteIndex(block_palette, palette_index,
                                            block_id)
             ? block_id
             : BLOCK_AIR;
}

/// CPU reference for G5 compute face extract: one unmerged quad per exposed
/// Returns true for blocks that can be face-extracted on GPU. Solid opaque,
/// transparent (glass), and cutout (leaves) are all eligible. Fluids and
/// cross-instance blocks (flowers/grass) remain CPU-only.
inline bool IsGpuFaceExtractEligible(UBlockRegistry &registry, BlockId id)
{
  if (id == 0)
  {
    return false;
  }
  const BlockRenderStyle style = registry.GetRenderStyle(id);
  if (style == BlockRenderStyle::Fluid || style == BlockRenderStyle::Cross)
  {
    return false;
  }
  return registry.IsSolid(id) && !registry.IsLiquid(id);
}

/// Q4 WorkerCompute: same eligibility from pinned catalog (no live registry).
inline bool IsGpuFaceExtractEligible(const BlockDefinitionCatalog *catalog,
                                     BlockId id)
{
  if (id == 0 || !catalog)
  {
    return false;
  }
  const BlockRenderStyle style = CatalogGetRenderStyle(catalog, id);
  if (style == BlockRenderStyle::Fluid || style == BlockRenderStyle::Cross)
  {
    return false;
  }
  // IsSolid ≡ BlocksMovement on the registry path.
  const auto *definition = FindCatalogDefinition(catalog, id);
  return CatalogBlocksMovement(catalog, id) &&
         (!definition || !definition->Physics.IsLiquid);
}

inline bool SnapshotIsGpuExtractEligible(const ChunkMeshSnapshot &snap,
                                         UBlockRegistry &registry)
{
  if (std::any_of(snap.fluid_packed.begin(), snap.fluid_packed.end(),
                  [](uint8_t packed) { return packed != 0; }))
    return false;

  for (BlockId id : snap.blocks)
  {
    if (id == 0)
    {
      continue;
    }
    if (!IsGpuFaceExtractEligible(registry, id))
    {
      return false;
    }
  }
  return true;
}

inline bool SnapshotIsGpuExtractEligible(const ChunkMeshSnapshot &snap,
                                         const BlockDefinitionCatalog *catalog)
{
  if (std::any_of(snap.fluid_packed.begin(), snap.fluid_packed.end(),
                  [](uint8_t packed) { return packed != 0; }))
    return false;

  if (!catalog)
  {
    return false;
  }
  for (BlockId id : snap.blocks)
  {
    if (id == 0)
    {
      continue;
    }
    if (!IsGpuFaceExtractEligible(catalog, id))
    {
      return false;
    }
  }
  return true;
}

inline constexpr int kGpuLightPad = CHUNK_SIZE + 4;
inline constexpr int kGpuLightPadVolume =
    kGpuLightPad * kGpuLightPad * kGpuLightPad;

inline constexpr int kGpuFaceVisibilityPad = CHUNK_SIZE + 2 * kFaceGeometryHalo;
inline constexpr int kGpuFaceVisibilityVolume =
    kGpuFaceVisibilityPad * kGpuFaceVisibilityPad * kGpuFaceVisibilityPad;
inline size_t FaceVisibilityIndex(glm::ivec3 local)
{
  return static_cast<size_t>(
      ((local.y + kFaceGeometryHalo) * kGpuFaceVisibilityPad + local.z +
       kFaceGeometryHalo) *
          kGpuFaceVisibilityPad +
      local.x + kFaceGeometryHalo);
}
template <typename MaterialWord>
inline void BuildPaddedFaceVisibilityWith(const ChunkMeshSnapshot &snapshot,
                                          MaterialWord word_for,
                                          std::vector<uint32_t> &words)
{
  words.assign(kGpuFaceVisibilityVolume, 0);
  std::vector<uint32_t> material_words(1u << 16, 0);
  for (int y = -kFaceGeometryHalo; y < CHUNK_SIZE + kFaceGeometryHalo; ++y)
    for (int z = -kFaceGeometryHalo; z < CHUNK_SIZE + kFaceGeometryHalo; ++z)
      for (int x = -kFaceGeometryHalo; x < CHUNK_SIZE + kFaceGeometryHalo; ++x)
      {
        const glm::ivec3 local(x, y, z);
        if (ShouldSkipFaceForNeighbor(
                snapshot.GetNeighborLoadState(snapshot.ChunkOrigin() + local)))
          continue;
        const auto id = snapshot.GetBlock(snapshot.ChunkOrigin() + local);
        auto &material = material_words[id];
        if (!material)
          material = word_for(id);
        auto word = material;
        if (x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE && y < CHUNK_SIZE &&
            z < CHUNK_SIZE &&
            FluidCellHasActiveFluid(snapshot.GetFluidPackedLocal(local)))
          word &= ~kFaceEligible;
        words[FaceVisibilityIndex(local)] = word;
      }
}
inline void BuildPaddedFaceVisibility(const ChunkMeshSnapshot &snapshot,
                                      const BlockDefinitionCatalog *catalog,
                                      std::vector<uint32_t> &words)
{
  BuildPaddedFaceVisibilityWith(
      snapshot, [&](BlockId id) { return CubeFaceMaterialWord(id, catalog); },
      words);
}
inline void BuildPaddedFaceVisibility(const ChunkMeshSnapshot &snapshot,
                                      UBlockRegistry &registry,
                                      const BlockDefinitionCatalog *catalog,
                                      std::vector<uint32_t> &words)
{
  BuildPaddedFaceVisibilityWith(
      snapshot, [&](BlockId id)
      { return CubeFaceMaterialWord(id, registry, catalog); }, words);
}
inline bool CubeFaceVisible(const std::vector<uint32_t> &words,
                            glm::ivec3 local, glm::ivec3 direction)
{
  const auto decision =
      DecideCubeFace(words[FaceVisibilityIndex(local)],
                     words[FaceVisibilityIndex(local + direction)]);
  return decision == CubeFaceDecision::Visible;
}

/// Padded light: center plus the radius-2 shell used by FaceLightPacked
/// fallback.
inline void BuildPaddedLight(const ChunkMeshSnapshot &snap,
                             std::vector<uint8_t> &lights)
{
  lights.assign(static_cast<size_t>(kGpuLightPadVolume), 0);
  const int pad = kGpuLightPad;
  constexpr int halo = ChunkMeshSnapshot::kLightHaloRadius;
  for (int y = -halo; y < CHUNK_SIZE + halo; ++y)
  {
    for (int z = -halo; z < CHUNK_SIZE + halo; ++z)
    {
      for (int x = -halo; x < CHUNK_SIZE + halo; ++x)
      {
        const glm::ivec3 world = snap.ChunkOrigin() + glm::ivec3(x, y, z);
        const int pi = ((y + halo) * pad + (z + halo)) * pad + (x + halo);
        lights[static_cast<size_t>(pi)] = snap.GetLightPacked(world);
      }
    }
  }
}

/// Same rule as GreedyMesher::FaceLightPacked: prefer air-neighbor light,
/// then horizontal samples around the face (side-wall underside), then solid.
inline uint8_t FaceLightPackedSnap(const ChunkMeshSnapshot &snap,
                                   glm::ivec3 local_solid, int axis, int sign)
{
  glm::ivec3 air = local_solid;
  air[axis] += sign;
  const glm::ivec3 world_air = snap.ChunkOrigin() + air;
  const uint8_t face = snap.GetLightPacked(world_air);
  if (face != 0)
  {
    return face;
  }
  static constexpr glm::ivec3 kHoriz[] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
  uint8_t best = 0;
  int best_sum = 0;
  for (const glm::ivec3 &o : kHoriz)
  {
    const uint8_t packed = snap.GetLightPacked(world_air + o);
    const int sum = UnpackSky(packed) + UnpackBlock(packed);
    if (sum > best_sum)
    {
      best_sum = sum;
      best = packed;
    }
  }
  if (best != 0)
  {
    return best;
  }
  return snap.GetLightPacked(snap.ChunkOrigin() + local_solid);
}

/// Emit 1x1 quads for all eligible non-fluid materials using canonical rules.
inline std::vector<GreedyQuad>
ExtractOpaqueFacesCpu(const ChunkMeshSnapshot &snap, UBlockRegistry &registry)
{
  std::vector<GreedyQuad> quads;
  quads.reserve(256);
  std::vector<uint32_t> visibility;
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  BuildPaddedFaceVisibility(snap, registry, catalog.get(), visibility);
  const int n = CHUNK_SIZE;
  for (int y = 0; y < n; ++y)
  {
    for (int z = 0; z < n; ++z)
    {
      for (int x = 0; x < n; ++x)
      {
        // ChunkMeshSnapshot blocks use Chunk::LocalIndex (x + n*y + n^2*z).
        const BlockId id = snap.GetBlockLocal(glm::ivec3(x, y, z));
        if (!(visibility[FaceVisibilityIndex({x, y, z})] & kFaceEligible))
        {
          continue;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
          for (int sign : {-1, 1})
          {
            glm::ivec3 direction(0);
            direction[axis] = sign;
            if (!CubeFaceVisible(visibility, {x, y, z}, direction))
              continue;
            GreedyQuad q;
            q.axis = axis;
            // UGreedyMesher convention: slice = voxel coordinate; u/v =
            // (axis+1)%3,(axis+2)%3.
            q.slice = (axis == 0 ? x : (axis == 1 ? y : z));
            if (axis == 0)
            {
              q.u = y;
              q.v = z;
            }
            else if (axis == 1)
            {
              q.u = z;
              q.v = x;
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
    // The face-mask SSBO uses the GPU occupancy layout x + n*z + n^2*y.
    // Recover that voxel coordinate, then fetch its block in chunk storage
    // order.
    const int x = static_cast<int>(i % n);
    const int y = static_cast<int>(i / (n * n));
    const int z = static_cast<int>((i / n) % n);
    const BlockId id = snap.GetBlockLocal(glm::ivec3(x, y, z));
    auto emit = [&](int axis, int sign, uint32_t bit)
    {
      if ((m & bit) == 0)
      {
        return;
      }
      GreedyQuad q;
      q.axis = axis;
      q.slice = (axis == 0 ? x : (axis == 1 ? y : z)); // voxel coordinate
      if (axis == 0)
      {
        q.u = y;
        q.v = z;
      }
      else if (axis == 1)
      {
        q.u = z;
        q.v = x;
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
