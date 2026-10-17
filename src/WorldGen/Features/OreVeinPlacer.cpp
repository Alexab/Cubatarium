#include "WorldGen/Features/OreVeinPlacer.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace cutum
{

namespace
{

uint32_t MixHash3D(int x, int y, int z, uint32_t seed)
{
  uint32_t hash = seed;
  hash ^= static_cast<uint32_t>(x) * 0x8da6b343U;
  hash = (hash ^ (hash >> 16U)) * 0x7feb352dU;
  hash ^= static_cast<uint32_t>(y) * 0xd8163841U;
  hash = (hash ^ (hash >> 15U)) * 0x846ca68bU;
  hash ^= static_cast<uint32_t>(z) * 0xcb1ab31fU;
  hash ^= hash >> 16U;
  return hash;
}

uint32_t StableStringHash(const std::string &value)
{
  uint32_t hash = 2166136261U;
  for (const unsigned char c : value)
  {
    hash = (hash ^ c) * 16777619U;
  }
  return hash;
}

int FloorDiv(int value, int divisor)
{
  const int quotient = value / divisor;
  return quotient - ((value % divisor) < 0 ? 1 : 0);
}

bool MeetsSeedModulo(int y, uint32_t seed, int modulo)
{
  return modulo <= 0 ||
         (static_cast<uint32_t>(y) + seed) %
                 static_cast<uint32_t>(modulo) ==
             0U;
}

bool IsOreHostBlock(BlockId id, const std::array<BlockId, 6> &host_ids)
{
  return std::find(host_ids.begin(), host_ids.end(), id) != host_ids.end() &&
         id != BLOCK_AIR;
}

std::array<BlockId, 6> ResolveOreHostIds(WorldGenContext &ctx)
{
  return {ctx.Blocks.Stone,
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    "granite"),
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    "diorite"),
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    "andesite"),
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    "deepslate"),
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    "tuff")};
}

BlockId ResolveOreBlockId(WorldGenContext &ctx, const std::string &slot)
{
  if (slot == "ore_coal")
  {
    return ctx.Blocks.OreCoal;
  }
  if (slot == "ore_iron")
  {
    return ctx.Blocks.OreIron;
  }

  const WorldGenSlotSpec *spec = UWorldGenRefs::GetSlot(slot);
  if (!spec)
  {
    return BLOCK_AIR;
  }
  for (const std::string &block_name : spec->BlockNames)
  {
    const BlockId id = ResolvePackScatterBlockId(
        ctx.Registry, ctx.WorldgenOwnerPackId, block_name);
    if (id != BLOCK_AIR)
    {
      return id;
    }
  }
  return BLOCK_AIR;
}

std::vector<PackOreRule> DefaultOreRules()
{
  PackOreRule coal;
  coal.Slot = "ore_coal";
  coal.YPeak = 42;
  coal.YSpread = 36;
  coal.VeinSize = 6;
  coal.Rarity = 0.12f;
  coal.MaxSurfaceOffset = 5;

  PackOreRule iron;
  iron.Slot = "ore_iron";
  iron.YPeak = 24;
  iron.YSpread = 28;
  iron.VeinSize = 4;
  iron.Rarity = 0.08f;
  iron.BelowSeaLevel = true;

  return {coal, iron};
}

struct VeinAnchor
{
  int X{0};
  int Y{0};
  int Z{0};
  uint32_t Hash{0};
};

bool MakeVeinAnchor(const PackOreRule &rule, int cell_x, int cell_y,
                    int cell_z, int spacing, uint32_t seed, float density,
                    int y_min, int y_max, int sea_level,
                    VeinAnchor &anchor)
{
  uint32_t hash = MixHash3D(cell_x, cell_y, cell_z,
                            seed ^ StableStringHash(rule.Slot));
  anchor.X = cell_x * spacing + static_cast<int>(hash % spacing);
  hash = MixHash3D(cell_x, cell_y, cell_z, hash + 0x9e3779b9U);
  anchor.Y = cell_y * spacing + static_cast<int>(hash % spacing);
  hash = MixHash3D(cell_x, cell_y, cell_z, hash + 0x85ebca6bU);
  anchor.Z = cell_z * spacing + static_cast<int>(hash % spacing);
  anchor.Hash = MixHash3D(anchor.X, anchor.Y, anchor.Z,
                          seed + StableStringHash(rule.Slot));

  if (anchor.Y < y_min || anchor.Y > y_max ||
      (rule.BelowSeaLevel && anchor.Y >= sea_level) ||
      !MeetsSeedModulo(anchor.Y, seed, rule.SeedModulo))
  {
    return false;
  }

  const float y_factor = TriangularYFactor(anchor.Y, y_min, rule.YPeak, y_max);
  const float chance = std::clamp(rule.Rarity * density * 0.50f * y_factor,
                                  0.0f, 0.9f);
  if (chance <= 0.0f)
  {
    return false;
  }
  const uint32_t threshold =
      static_cast<uint32_t>(chance * 4294967295.0f);
  return anchor.Hash <= threshold;
}

bool IsInsideVein(const PackOreRule &rule, const VeinAnchor &anchor, int x,
                  int y, int z)
{
  const float size = std::sqrt(static_cast<float>(
      std::clamp(rule.VeinSize, 1, 32)) / 6.0f);
  const float radius_x = std::max(1.25f, 2.35f * size);
  const float radius_y = std::max(0.9f, 1.35f * size);
  const float radius_z = std::max(1.25f, 2.15f * size);
  const float dx = static_cast<float>(x - anchor.X) / radius_x;
  const float dy = static_cast<float>(y - anchor.Y) / radius_y;
  const float dz = static_cast<float>(z - anchor.Z) / radius_z;
  const float distance = dx * dx + dy * dy + dz * dz;
  const float irregularity =
      (static_cast<float>(MixHash3D(x, y, z, anchor.Hash) & 0xffffU) /
           65535.0f -
       0.5f) *
      0.24f;
  return distance <= 1.0f + irregularity;
}

bool TryPlaceOreRule(const WorldGenContext &ctx, const PackOreRule &rule,
                     BlockId ore_id, int x, int y, int z, int surface_y,
                     uint32_t seed, float density)
{
  if (ore_id == BLOCK_AIR || y >= surface_y - rule.MaxSurfaceOffset)
  {
    return false;
  }
  if (rule.BelowSeaLevel && y >= ctx.Settings.SeaLevel)
  {
    return false;
  }
  if (!MeetsSeedModulo(y, seed, rule.SeedModulo))
  {
    return false;
  }

  const int y_min = std::max(1, rule.YPeak - rule.YSpread);
  const int y_max = rule.YPeak + rule.YSpread;
  if (y < y_min || y > y_max)
  {
    return false;
  }

  const int vein_size = std::clamp(rule.VeinSize, 1, 32);
  const int spacing = std::clamp(6 + vein_size, 8, 40);
  const int cell_x = FloorDiv(x, spacing);
  const int cell_y = FloorDiv(y, spacing);
  const int cell_z = FloorDiv(z, spacing);
  for (int anchor_y = cell_y - 1; anchor_y <= cell_y + 1; ++anchor_y)
  {
    for (int anchor_z = cell_z - 1; anchor_z <= cell_z + 1; ++anchor_z)
    {
      for (int anchor_x = cell_x - 1; anchor_x <= cell_x + 1; ++anchor_x)
      {
        VeinAnchor anchor;
        if (!MakeVeinAnchor(rule, anchor_x, anchor_y, anchor_z, spacing, seed,
                            density, y_min, y_max, ctx.Settings.SeaLevel,
                            anchor))
        {
          continue;
        }
        if (IsInsideVein(rule, anchor, x, y, z))
        {
          ctx.World.SetBlock(glm::ivec3(x, y, z), ore_id);
          return true;
        }
      }
    }
  }
  return false;
}

} // namespace

void FillOreVeins(WorldGenContext &ctx, int x, int z, int surfaceY,
                  uint32_t seed, float oreDensity)
{
  if (!ctx.Settings.EnableOres || oreDensity <= 0.0f)
  {
    return;
  }

  const PackOresConfig &pack_ores = UWorldGenPack::OresConfig();
  const std::vector<PackOreRule> rules =
      pack_ores.Loaded ? pack_ores.Rules : DefaultOreRules();
  if (rules.empty())
  {
    return;
  }

  const int min_y = 1;
  const int max_y = std::max(min_y, surfaceY - 3);
  const float density = std::clamp(oreDensity, 0.0f, 2.0f);
  const std::array<BlockId, 6> host_ids = ResolveOreHostIds(ctx);
  std::vector<std::pair<const PackOreRule *, BlockId>> resolved_rules;
  resolved_rules.reserve(rules.size());
  for (const PackOreRule &rule : rules)
  {
    resolved_rules.emplace_back(&rule, ResolveOreBlockId(ctx, rule.Slot));
  }
  bool placed = false;
  for (int y = min_y; y <= max_y; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    const BlockId host_id = ctx.World.GetBlock(pos);
    if (!IsOreHostBlock(host_id, host_ids))
    {
      continue;
    }

    for (const auto &[rule, ore_id] : resolved_rules)
    {
      if (TryPlaceOreRule(ctx, *rule, ore_id, x, y, z, surfaceY, seed,
                          density))
      {
        placed = true;
        break;
      }
    }
  }
  if (placed)
  {
    ctx.AccumulateDirtyColumn(min_y, max_y);
  }
}

} // namespace cutum
