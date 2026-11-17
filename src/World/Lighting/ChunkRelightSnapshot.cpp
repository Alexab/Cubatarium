#include "World/Lighting/ChunkRelightSnapshot.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <tuple>

namespace cutum
{
namespace
{
using Geometry = std::array<BlockId, CHUNK_VOLUME>;
using LightData = std::array<uint8_t, CHUNK_VOLUME>;

bool CoordLess(glm::ivec3 a, glm::ivec3 b)
{
  return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
}

bool InChunkLocal(glm::ivec3 p)
{
  return p.x >= 0 && p.x < CHUNK_SIZE && p.y >= 0 && p.y < CHUNK_SIZE &&
         p.z >= 0 && p.z < CHUNK_SIZE;
}

// One immutable material view for the whole computation. Registry lookups
// during a flood could otherwise observe different catalogs after a reload.
class LightMaterials
{
public:
  struct Info
  {
    bool transparent{false};
    bool liquid{false};
    uint8_t emission{0};
    bool known{false};
  };
  LightMaterials(std::shared_ptr<const BlockDefinitionCatalog> catalog,
                 const UBlockRegistry &fallback)
      : Catalog(std::move(catalog)), Fallback(fallback),
        Entries(static_cast<size_t>(std::numeric_limits<BlockId>::max()) + 1)
  {
    Entries[BLOCK_AIR] = {true, false, 0, true};
    if (Catalog)
      for (const auto &entry : Catalog->ById)
      {
        if (entry.first == BLOCK_AIR)
          continue;
        const auto &d = entry.second;
        Entries[entry.first] = {d.Physics.IsLiquid ||
                                    d.Render.Style == BlockRenderStyle::Cross ||
                                    !(d.Physics.Movement.Occupancy >= 1.0f),
                                d.Physics.IsLiquid,
                                static_cast<uint8_t>(std::clamp(
                                    d.Lighting.Emission, 0, kMaxLightLevel)),
                                true};
      }
  }
  const Info &Get(BlockId id)
  {
    auto &info = Entries[id];
    if (!info.known && !Catalog)
      info = {IsLightTransparent(Fallback, id), Fallback.IsLiquid(id),
              static_cast<uint8_t>(
                  std::clamp(Fallback.GetLightEmission(id), 0, kMaxLightLevel)),
              true};
    if (!info.known)
      UnknownMaterialRead = true;
    return info;
  }
  bool Complete() const { return Catalog && !UnknownMaterialRead; }

private:
  std::shared_ptr<const BlockDefinitionCatalog> Catalog;
  const UBlockRegistry &Fallback;
  std::vector<Info> Entries;
  bool UnknownMaterialRead{false};
};

} // namespace

BlockId UChunkRelightSnapshot::GetBlock(glm::ivec3 p) const
{
  const auto *data = GeometryData(UChunkManager::WorldToChunk(p));
  return data ? (*data)[static_cast<size_t>(
                    UChunk::LocalIndex(UChunkManager::WorldToLocal(p)))]
              : BLOCK_AIR;
}

const Geometry *UChunkRelightSnapshot::GeometryData(glm::ivec3 c) const
{
  const auto main = Blocks.find(c);
  if (main != Blocks.end())
    return &main->second;
  const auto upper = SkyContextBlocks.find(c);
  return upper == SkyContextBlocks.end() ? nullptr : &upper->second;
}
bool UChunkRelightSnapshot::HasChunk(glm::ivec3 c) const
{
  return WriteChunks.count(c) != 0;
}
bool UChunkRelightSnapshot::HasComputeChunk(glm::ivec3 c) const
{
  return ComputeChunks.count(c) != 0;
}
bool UChunkRelightSnapshot::IsKnownGeometry(glm::ivec3 c) const
{
  return KnownGeometry.count(c) != 0;
}
bool UChunkRelightSnapshot::HasLight(glm::ivec3 c) const
{
  return Light.count(c) != 0;
}
int UChunkRelightSnapshot::GetSkyLight(glm::ivec3 p) const
{
  return GetSkyLightLocal(UChunkManager::WorldToChunk(p),
                          UChunkManager::WorldToLocal(p));
}
int UChunkRelightSnapshot::GetBlockLight(glm::ivec3 p) const
{
  return GetBlockLightLocal(UChunkManager::WorldToChunk(p),
                            UChunkManager::WorldToLocal(p));
}
int UChunkRelightSnapshot::GetSkyLightLocal(glm::ivec3 c, glm::ivec3 p) const
{
  const auto it = Light.find(c);
  return it == Light.end() || !InChunkLocal(p)
             ? 0
             : UnpackSky(it->second[UChunk::LocalIndex(p)]);
}
int UChunkRelightSnapshot::GetBlockLightLocal(glm::ivec3 c, glm::ivec3 p) const
{
  const auto it = Light.find(c);
  return it == Light.end() || !InChunkLocal(p)
             ? 0
             : UnpackBlock(it->second[UChunk::LocalIndex(p)]);
}
void UChunkRelightSnapshot::SetLightLocal(glm::ivec3 c, glm::ivec3 p, int sky,
                                          int block)
{
  const auto it = Light.find(c);
  if (it != Light.end() && InChunkLocal(p))
    it->second[UChunk::LocalIndex(p)] = PackLight(sky, block);
}
void UChunkRelightSnapshot::WriteSkyLight(glm::ivec3 p, int level)
{
  SetLightLocal(UChunkManager::WorldToChunk(p), UChunkManager::WorldToLocal(p),
                level, GetBlockLight(p));
}
void UChunkRelightSnapshot::WriteBlockLight(glm::ivec3 p, int level)
{
  SetLightLocal(UChunkManager::WorldToChunk(p), UChunkManager::WorldToLocal(p),
                GetSkyLight(p), level);
}
void UChunkRelightSnapshot::ClearChunkLight(glm::ivec3 c)
{
  const auto it = Light.find(c);
  if (it != Light.end())
    it->second.fill(0);
}

UChunkRelightSnapshot UChunkRelightSnapshot::Capture(const UBlockWorld &world,
                                                     const RelightJobSpec &spec)
{
  return CaptureImpl(world, spec, nullptr);
}

UChunkRelightSnapshot
UChunkRelightSnapshot::CaptureSections(const UBlockWorld &world,
                                       const RelightJobSpec &spec,
                                       const std::vector<glm::ivec3> &sections)
{
  return CaptureImpl(world, spec, &sections);
}

UChunkRelightSnapshot
UChunkRelightSnapshot::CaptureImpl(const UBlockWorld &world,
                                   const RelightJobSpec &spec,
                                   const std::vector<glm::ivec3> *sections)
{
  UChunkRelightSnapshot snapshot;
  snapshot.Spec = spec;
  snapshot.InputProfile = world.GetVerticalProfile();
  snapshot.WorldMaxY = snapshot.InputProfile.MaxY;
  snapshot.InputLightingConfiguration =
      world.GetLightingConfigurationIdentity();
  snapshot.InputContextCaptured = snapshot.InputProfile.IsValid();
  if (!snapshot.InputContextCaptured || spec.max_world_y < spec.min_world_y)
    return snapshot;
  const int low_y = std::max(spec.min_world_y, snapshot.InputProfile.MinY);
  const int high_y = std::min(spec.max_world_y, snapshot.InputProfile.MaxY);
  if (high_y < low_y)
    return snapshot;
  std::unordered_set<glm::ivec3, IVec3Hash> targets;
  if (sections)
  {
    for (const auto c : *sections)
      if (c.y >= FloorDiv(low_y, CHUNK_SIZE) &&
          c.y <= FloorDiv(high_y, CHUNK_SIZE))
        targets.insert(c);
  }
  else
  {
    for (const auto source : spec.block_positions)
    {
      const auto ground = UChunkManager::WorldToChunk(source);
      const int radius = spec.column_center_only ? 0 : 1;
      for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz)
          for (int cy = FloorDiv(low_y, CHUNK_SIZE);
               cy <= FloorDiv(high_y, CHUNK_SIZE); ++cy)
            targets.insert({ground.x + dx, cy, ground.z + dz});
    }
  }
  std::unordered_set<glm::ivec3, IVec3Hash> context;
  if (!sections)
  {
    for (const auto coord : spec.known_air_output_sections)
      if (snapshot.InputProfile.SliceBounds(coord.y, CHUNK_SIZE) &&
          !world.GetChunkManager().HasChunk(coord))
        targets.insert(coord);
    for (const auto coord : spec.light_sample_output_sections)
      if (snapshot.InputProfile.SliceBounds(coord.y, CHUNK_SIZE))
        targets.insert(coord);
  }
  for (const auto target : targets)
  {
    if (world.GetChunkManager().HasChunk(target) ||
        (((sections && spec.include_known_air_outputs) ||
          std::find(spec.known_air_output_sections.begin(),
                    spec.known_air_output_sections.end(),
                    target) != spec.known_air_output_sections.end() ||
          std::find(spec.light_sample_output_sections.begin(),
                    spec.light_sample_output_sections.end(),
                    target) != spec.light_sample_output_sections.end()) &&
         world.CaptureInputStamp(target, false, true).missingEvidence ==
             MissingChunkEvidence::KnownAir))
      snapshot.WriteChunks.insert(target);
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
        {
          const auto c = target + glm::ivec3(dx, dy, dz);
          if (snapshot.InputProfile.SliceBounds(c.y, CHUNK_SIZE))
            context.insert(c);
        }
  }
  std::vector<glm::ivec3> ordered(context.begin(), context.end());
  std::sort(ordered.begin(), ordered.end(), CoordLess);
  std::unordered_map<glm::ivec3, int, IVec3Hash> bottoms;
  for (const auto c : ordered)
  {
    snapshot.ReadSet.push_back(world.CaptureInputStamp(c, false, true));
    const UChunk *chunk = world.GetChunkManager().GetChunk(c);
    const bool known = chunk || snapshot.ReadSet.back().missingEvidence ==
                                    MissingChunkEvidence::KnownAir;
    auto [it, inserted] = bottoms.emplace(glm::ivec3(c.x, 0, c.z), c.y);
    if (!inserted)
      it->second = std::min(it->second, c.y);
    if (!known)
      continue;
    snapshot.KnownGeometry.insert(c);
    snapshot.ComputeChunks.insert(c);
    snapshot.Light.emplace(c, LightData{});
    if (chunk)
    {
      if (!chunk->IsAirOnly())
        snapshot.Blocks.emplace(c, chunk->GetData());
      if (snapshot.WriteChunks.count(c))
        ++snapshot.CapturedFullChunks;
      else
        ++snapshot.CapturedGeometryHaloChunks;
    }
  }
  if (spec.include_skylight)
  {
    const auto started = std::chrono::steady_clock::now();
    ordered.clear();
    for (const auto &bottom : bottoms)
      for (int cy = bottom.second;
           cy <= snapshot.InputProfile.MaxChunkY(CHUNK_SIZE); ++cy)
        if (!context.count({bottom.first.x, cy, bottom.first.z}))
          ordered.emplace_back(bottom.first.x, cy, bottom.first.z);
    std::sort(ordered.begin(), ordered.end(), CoordLess);
    for (const auto c : ordered)
    {
      snapshot.ReadSet.push_back(world.CaptureInputStamp(c, false, true));
      const UChunk *chunk = world.GetChunkManager().GetChunk(c);
      if (chunk || snapshot.ReadSet.back().missingEvidence ==
                       MissingChunkEvidence::KnownAir)
        snapshot.KnownGeometry.insert(c);
      if (chunk && !chunk->IsAirOnly())
      {
        snapshot.SkyContextBlocks.emplace(c, chunk->GetData());
        ++snapshot.CapturedSkyContextChunks;
      }
    }
    snapshot.SkyContextCaptureMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();
  }
  return snapshot;
}

RelightComputeResult
UChunkRelightSnapshot::Compute(const UBlockRegistry &registry)
{
  RelightComputeResult result;
  result.read_set = ReadSet;
  result.input_profile = InputProfile;
  result.input_lighting_configuration = InputLightingConfiguration;
  result.input_context_captured = InputContextCaptured;
  result.input_catalog = InputCatalogPinned
                             ? InputCatalog
                             : registry.GetDefinitionsCatalogSnapshot();
  result.retry_spec = Spec;
  result.job_id = Spec.job_id;
  result.source_block_positions = Spec.block_positions;
  result.finalize_pending_gate = Spec.finalize_pending_gate;
  result.visible_draw_gate_repair = Spec.visible_draw_gate_repair;
  result.include_skylight = Spec.include_skylight;
  result.include_block_light = Spec.include_block_light;
  result.work_token = CapturedWorkToken;
  result.dependency_stamp = CapturedDepStamp;
  for (const auto &stamp : ReadSet)
    if (stamp.incarnation == 0 &&
        stamp.missingEvidence != MissingChunkEvidence::KnownAir)
      ++result.unknown_input_chunks;
  for (auto &entry : Light)
    entry.second.fill(0);
  LightMaterials materials(result.input_catalog, registry);
  if (Spec.include_skylight)
  {
    // Carry all 256 incoming columns through each upper section once. Empty
    // read-only sections cost one lookup, not 4096 per-cell world lookups.
    std::unordered_map<glm::ivec3, int, IVec3Hash> bottoms;
    for (const auto c : ComputeChunks)
    {
      auto [it, inserted] = bottoms.emplace(glm::ivec3(c.x, 0, c.z), c.y);
      if (!inserted)
        it->second = std::min(it->second, c.y);
    }
    for (const auto &bottom : bottoms)
    {
      std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE> incoming;
      incoming.fill(kMaxLightLevel);
      for (int cy = InputProfile.MaxChunkY(CHUNK_SIZE); cy >= bottom.second;
           --cy)
      {
        const glm::ivec3 c(bottom.first.x, cy, bottom.first.z);
        if (!IsKnownGeometry(c))
          break; // unknown cannot act as clear sky
        const auto *data = GeometryData(c);
        const auto light = Light.find(c);
        if (!data && light == Light.end())
          continue;
        const auto bounds = InputProfile.SliceBounds(cy, CHUNK_SIZE);
        if (!bounds)
          continue;
        const int base_y = cy * CHUNK_SIZE;
        for (int y = bounds->second - base_y; y >= bounds->first - base_y; --y)
          for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              auto &level = incoming[x + CHUNK_SIZE * z];
              const auto index =
                  static_cast<size_t>(UChunk::LocalIndex({x, y, z}));
              const auto &info =
                  materials.Get(data ? (*data)[index] : BLOCK_AIR);
              if (!info.transparent)
                level = 0;
              if (light != Light.end())
                light->second[index] = PackLight(level, 0);
              if (info.liquid && level)
                --level;
            }
      }
    }
  }
  if (Spec.include_block_light)
    for (const auto c : ComputeChunks)
      if (const auto *data = GeometryData(c))
        for (int y = 0; y < CHUNK_SIZE; ++y)
          for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              if (!InputProfile.ContainsY(c.y * CHUNK_SIZE + y))
                continue;
              const auto &info =
                  materials.Get((*data)[UChunk::LocalIndex({x, y, z})]);
              if (info.emission)
                WriteBlockLight(c * CHUNK_SIZE + glm::ivec3(x, y, z),
                                info.emission);
            }
  // Resolve topology once per computation. The flood touches millions of
  // neighbors in a tall band; world-coordinate hashes in that inner loop can
  // otherwise dominate the bounded solver. No map is mutated below.
  struct Tile
  {
    glm::ivec3 coord;
    const Geometry *geometry;
    LightData *light;
    int min_local_y;
    int max_local_y;
    std::array<Tile *, 6> neighbors{};
  };
  std::vector<Tile> tiles;
  tiles.reserve(Light.size());
  std::unordered_map<glm::ivec3, size_t, IVec3Hash> tile_indices;
  for (auto &entry : Light)
  {
    const auto bounds = InputProfile.SliceBounds(entry.first.y, CHUNK_SIZE);
    if (!bounds)
      continue;
    tile_indices.emplace(entry.first, tiles.size());
    tiles.push_back({entry.first,
                     GeometryData(entry.first),
                     &entry.second,
                     bounds->first - entry.first.y * CHUNK_SIZE,
                     bounds->second - entry.first.y * CHUNK_SIZE,
                     {}});
  }
  for (auto &tile : tiles)
    for (size_t face = 0; face < 6; ++face)
    {
      const auto it = tile_indices.find(tile.coord + NEIGHBOR_OFFSETS[face]);
      if (it != tile_indices.end())
        tile.neighbors[face] = &tiles[it->second];
    }
  const auto flood = [&](bool sky)
  {
    static_assert(CHUNK_VOLUME <=
                  static_cast<int>(std::numeric_limits<uint16_t>::max()) + 1);
    struct Node
    {
      Tile *tile;
      uint16_t index;
    };
    std::array<std::deque<Node>, kMaxLightLevel + 1> buckets;
    for (auto &tile : tiles)
      for (int i = 0; i < CHUNK_VOLUME; ++i)
      {
        const auto packed = (*tile.light)[i];
        const int level = sky ? UnpackSky(packed) : UnpackBlock(packed);
        if (level > 1)
          buckets[level].push_back({&tile, static_cast<uint16_t>(i)});
      }
    for (int level = kMaxLightLevel; level > 1; --level)
      while (!buckets[level].empty())
      {
        const auto node = buckets[level].front();
        buckets[level].pop_front();
        const auto packed = (*node.tile->light)[node.index];
        if ((sky ? UnpackSky(packed) : UnpackBlock(packed)) != level)
          continue;
        const glm::ivec3 local(node.index % CHUNK_SIZE,
                               (node.index / CHUNK_SIZE) % CHUNK_SIZE,
                               node.index / (CHUNK_SIZE * CHUNK_SIZE));
        for (size_t face = 0; face < 6; ++face)
        {
          auto next_local = local + NEIGHBOR_OFFSETS[face];
          Tile *tile = node.tile;
          if (!InChunkLocal(next_local))
          {
            tile = tile->neighbors[face];
            if (!tile)
              continue;
            for (int axis = 0; axis < 3; ++axis)
            {
              if (next_local[axis] < 0)
                next_local[axis] += CHUNK_SIZE;
              else if (next_local[axis] >= CHUNK_SIZE)
                next_local[axis] -= CHUNK_SIZE;
            }
          }
          if (next_local.y < tile->min_local_y ||
              next_local.y > tile->max_local_y)
            continue;
          const int index = UChunk::LocalIndex(next_local);
          auto &target = (*tile->light)[index];
          const int current = sky ? UnpackSky(target) : UnpackBlock(target);
          if (current >= level - 1)
            continue;
          const auto &info = materials.Get(
              tile->geometry ? (*tile->geometry)[index] : BLOCK_AIR);
          if (!info.transparent)
            continue;
          const int next = level - (sky && info.liquid ? 2 : 1);
          if (next <= current)
            continue;
          target = sky ? PackLight(next, UnpackBlock(target))
                       : PackLight(UnpackSky(target), next);
          if (next > 1)
            buckets[next].push_back({tile, static_cast<uint16_t>(index)});
        }
      }
  };
  if (Spec.include_skylight)
    flood(true);
  if (Spec.include_block_light)
    flood(false);
  result.inputs_complete = InputContextCaptured && !ReadSet.empty() &&
                           result.unknown_input_chunks == 0 &&
                           materials.Complete();
  std::vector<glm::ivec3> targets(WriteChunks.begin(), WriteChunks.end());
  std::sort(targets.begin(), targets.end(), CoordLess);
  for (const auto c : targets)
  {
    const auto it = Light.find(c);
    if (it == Light.end())
      continue;
    auto padded = std::make_shared<SectionLightVolume>();
    for (int z = -SectionLightVolume::Radius;
         z < CHUNK_SIZE + SectionLightVolume::Radius; ++z)
      for (int y = -SectionLightVolume::Radius;
           y < CHUNK_SIZE + SectionLightVolume::Radius; ++y)
        for (int x = -SectionLightVolume::Radius;
             x < CHUNK_SIZE + SectionLightVolume::Radius; ++x)
        {
          const auto position = c * CHUNK_SIZE + glm::ivec3(x, y, z);
          uint8_t packed = 0;
          if (position.y > InputProfile.MaxY)
            packed = Spec.include_skylight ? PackLight(kMaxLightLevel, 0) : 0;
          else if (position.y >= InputProfile.MinY)
          {
            const auto source =
                Light.find(UChunkManager::WorldToChunk(position));
            if (source != Light.end())
              packed = source->second[UChunk::LocalIndex(
                  UChunkManager::WorldToLocal(position))];
          }
          padded->cells[SectionLightVolume::Index(x, y, z)] = packed;
        }
    result.chunks.push_back({c, it->second, std::move(padded)});
  }
  return result;
}
} // namespace cutum
