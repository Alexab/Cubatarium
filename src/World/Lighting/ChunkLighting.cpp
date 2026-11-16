#include "World/Lighting/ChunkLighting.h"

#include "App/Platform/Log.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Environment.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/LightUtil.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

namespace cutum
{
namespace
{
std::vector<glm::ivec3>
RelightChunkCoords(UBlockWorld &world, UBlockRegistry &registry,
                   const std::vector<glm::ivec3> &coords,
                   bool include_block_light, bool include_skylight,
                   bool clear_first = true)
{
  // Scratch computation always starts from geometry. The historical clear flag
  // no longer exposes a half-cleared live field or changes the result.
  (void)clear_first;
  if (coords.empty() || (!include_skylight && !include_block_light))
    return {};
  const bool audit = IsEnvironmentFlagEnabled("CUBATARIUM_RELIGHT_AUDIT");
  const auto capture_started = std::chrono::steady_clock::now();
  RelightJobSpec spec;
  spec.min_world_y = world.GetVerticalProfile().MinY;
  spec.max_world_y = world.GetVerticalProfile().MaxY;
  spec.include_skylight = include_skylight;
  spec.include_block_light = include_block_light;
  auto snapshot = UChunkRelightSnapshot::CaptureSections(world, spec, coords);
  snapshot.SetInputCatalog(registry.GetDefinitionsCatalogSnapshot());
  const auto captured_at = std::chrono::steady_clock::now();
  const auto result = snapshot.Compute(registry);
  const auto computed_at = std::chrono::steady_clock::now();
  // Validate the complete batch before publishing any domain. Unknown inputs
  // remain a transient result, never a completion certificate.
  if (ValidateRelightInputs(result, world,
                            registry.GetDefinitionsCatalogSnapshot()) !=
      RelightInputValidation::Current)
    return {};
  std::vector<glm::ivec3> changed;
  for (const auto &data : result.chunks)
    if (auto *chunk = world.GetChunkManager().GetChunk(data.coord))
      if (InstallComputedLight(*chunk, data.light_packed, include_skylight,
                               include_block_light))
        changed.push_back(data.coord);
  if (audit)
    CubatariumLogInfo(
        "RelightAudit",
        "sync_geometry event=apply requested=" + std::to_string(coords.size()) +
            " produced=" + std::to_string(result.chunks.size()) +
            " changed=" + std::to_string(changed.size()) +
            " include_sky=" + std::to_string(include_skylight) +
            " include_block=" + std::to_string(include_block_light) +
            " inputs_complete=" + std::to_string(result.inputs_complete) +
            " unknown_inputs=" + std::to_string(result.unknown_input_chunks) +
            " capture_ms=" +
            std::to_string(std::chrono::duration<double, std::milli>(
                               captured_at - capture_started)
                               .count()) +
            " compute_ms=" +
            std::to_string(std::chrono::duration<double, std::milli>(
                               computed_at - captured_at)
                               .count()));
  return changed;
}

} // namespace

void PropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 chunk_coord)
{
  RelightChunkCoords(world, registry, {chunk_coord}, true, false);
}

namespace
{

void CollectColumnChunkCoords(const UBlockWorld &world, int world_x,
                              int world_z, int min_y, int max_y,
                              std::unordered_set<glm::ivec3, IVec3Hash> &out)
{
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, min_y, world_z));
  const glm::ivec3 top =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, max_y, world_z));
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int cy = base.y; cy <= top.y; ++cy)
      {
        const glm::ivec3 coord(base.x + dx, cy, base.z + dz);
        if (world.GetChunkManager().HasChunk(coord))
        {
          out.insert(coord);
        }
      }
    }
  }
}

bool FaceHasLitTransparentVoxel(const UBlockWorld &world,
                                const UBlockRegistry &registry,
                                const UChunk &chunk, glm::ivec3 chunk_coord,
                                const glm::ivec3 &neighbor_offset)
{
  const auto check_face =
      [&](int fixed_axis, int fixed_value, int axis_a, int axis_b)
  {
    for (int a = 0; a < CHUNK_SIZE; ++a)
    {
      for (int b = 0; b < CHUNK_SIZE; ++b)
      {
        glm::ivec3 local(0);
        local[fixed_axis] = fixed_value;
        local[axis_a] = a;
        local[axis_b] = b;
        if (!IsLightTransparent(registry, chunk.GetBlockLocal(local)))
        {
          continue;
        }
        if (chunk.GetSkyLightLocal(local) > 0 ||
            chunk.GetBlockLightLocal(local) > 0)
        {
          return true;
        }
      }
    }
    return false;
  };

  if (neighbor_offset.x == 1)
  {
    return check_face(0, CHUNK_SIZE - 1, 1, 2);
  }
  if (neighbor_offset.x == -1)
  {
    return check_face(0, 0, 1, 2);
  }
  if (neighbor_offset.y == 1)
  {
    return check_face(1, CHUNK_SIZE - 1, 0, 2);
  }
  if (neighbor_offset.y == -1)
  {
    return check_face(1, 0, 0, 2);
  }
  if (neighbor_offset.z == 1)
  {
    return check_face(2, CHUNK_SIZE - 1, 0, 1);
  }
  if (neighbor_offset.z == -1)
  {
    return check_face(2, 0, 0, 1);
  }
  return false;
}

void CollectLitBorderNeighborChunks(
    const UBlockWorld &world, const UBlockRegistry &registry,
    glm::ivec3 chunk_coord,
    const std::unordered_set<glm::ivec3, IVec3Hash> &relit_set,
    std::unordered_set<glm::ivec3, IVec3Hash> &frontier)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return;
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor_coord = chunk_coord + offset;
    if (relit_set.count(neighbor_coord) ||
        !world.GetChunkManager().HasChunk(neighbor_coord))
    {
      continue;
    }
    if (FaceHasLitTransparentVoxel(world, registry, *chunk, chunk_coord,
                                   offset))
    {
      frontier.insert(neighbor_coord);
    }
  }
}

std::unordered_set<glm::ivec3, IVec3Hash> RelightChunkSetWithFrontier(
    UBlockWorld &world, UBlockRegistry &registry,
    std::unordered_set<glm::ivec3, IVec3Hash> initial_coords,
    bool include_block_light, int frontier_iterations,
    bool *out_frontier_unfinished, bool include_skylight = true)
{
  if (initial_coords.empty())
  {
    if (out_frontier_unfinished)
    {
      *out_frontier_unfinished = false;
    }
    return {};
  }

  std::vector<glm::ivec3> batch(initial_coords.begin(), initial_coords.end());
  RelightChunkCoords(world, registry, batch, include_block_light,
                     include_skylight);

  std::unordered_set<glm::ivec3, IVec3Hash> relit_set =
      std::move(initial_coords);

  bool frontier_unfinished = false;
  for (int iter = 0; iter < frontier_iterations; ++iter)
  {
    std::unordered_set<glm::ivec3, IVec3Hash> frontier;
    for (const glm::ivec3 &coord : relit_set)
    {
      CollectLitBorderNeighborChunks(world, registry, coord, relit_set,
                                     frontier);
    }
    if (frontier.empty())
    {
      frontier_unfinished = false;
      break;
    }
    batch.assign(frontier.begin(), frontier.end());
    RelightChunkCoords(world, registry, batch, include_block_light,
                       include_skylight);
    relit_set.insert(frontier.begin(), frontier.end());
    frontier_unfinished = (iter == frontier_iterations - 1);
  }
  if (out_frontier_unfinished)
  {
    *out_frontier_unfinished = frontier_unfinished;
  }
  return relit_set;
}

void CollectEditNeighborhoodChunkCoords(
    const UBlockWorld &world, const std::vector<glm::ivec3> &block_positions,
    std::unordered_set<glm::ivec3, IVec3Hash> &out)
{
  // Block light has finite radius, but opening a sky shaft can affect every
  // lower loaded slice. Include its horizontal influence halo and all lower
  // geometry; do not rely on whichever neighboring light happens to be ready.
  const auto &profile = world.GetVerticalProfile();
  for (const auto block_pos : block_positions)
  {
    const int top_y = static_cast<int>(std::min<int64_t>(
        profile.MaxY, static_cast<int64_t>(block_pos.y) + kMaxLightLevel));
    if (top_y >= profile.MinY)
      CollectColumnChunkCoords(world, block_pos.x, block_pos.z, profile.MinY,
                               top_y, out);
  }
}

} // namespace

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light,
                  bool include_skylight)
{
  if (!world.GetChunkManager().HasChunk(chunk_coord))
  {
    return;
  }
  RelightChunkCoords(world, registry, {chunk_coord}, include_block_light,
                     include_skylight);
}

void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                            glm::ivec3 chunk_coord)
{
  RelightChunkCoords(world, registry, {chunk_coord}, true, false);
}

void RelightBlocksAroundLocal(UBlockWorld &world, UBlockRegistry &registry,
                              const std::vector<glm::ivec3> &block_positions,
                              bool clear_first)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectEditNeighborhoodChunkCoords(world, block_positions, coords);
  if (coords.empty())
  {
    return;
  }
  const std::vector<glm::ivec3> batch(coords.begin(), coords.end());
  RelightChunkCoords(world, registry, batch, true, true, clear_first);
}

void RelightBlocksAroundEdit(UBlockWorld &world, UBlockRegistry &registry,
                             const std::vector<glm::ivec3> &block_positions)
{
  RelightBlocksAroundLocal(world, registry, block_positions);
}

void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos, int max_world_y)
{
  (void)max_world_y;
  RelightBlocksAroundLocal(world, registry, {block_pos});
}

std::vector<glm::ivec3>
RelightBlocksAroundAll(UBlockWorld &world, UBlockRegistry &registry,
                       const std::vector<glm::ivec3> &block_positions,
                       int max_world_y)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    CollectColumnChunkCoords(world, block_pos.x, block_pos.z,
                             world.GetVerticalProfile().MinY, max_world_y,
                             coords);
  }
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(world, registry, std::move(coords), true,
                                  kRelightFrontierIterationsFull, nullptr);
  return {relit.begin(), relit.end()};
}

RelightFrontierOutcome
RelightBlocksAroundAllEx(UBlockWorld &world, UBlockRegistry &registry,
                         const std::vector<glm::ivec3> &block_positions,
                         int min_world_y, int max_world_y,
                         bool include_block_light, int frontier_iterations)
{
  RelightFrontierOutcome outcome;
  if (block_positions.empty())
  {
    return outcome;
  }
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    CollectColumnChunkCoords(world, block_pos.x, block_pos.z, min_world_y,
                             max_world_y, coords);
  }
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(world, registry, std::move(coords),
                                  include_block_light, frontier_iterations,
                                  &outcome.frontier_unfinished);
  outcome.relit_chunks.assign(relit.begin(), relit.end());
  return outcome;
}

void RelightColumnWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               bool include_block_light, bool include_skylight,
                               std::vector<glm::ivec3> *out_relit_chunks)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectColumnChunkCoords(world, world_x, world_z, min_y, max_y, coords);
  const std::unordered_set<glm::ivec3, IVec3Hash> relit =
      RelightChunkSetWithFrontier(
          world, registry, std::move(coords), include_block_light,
          kRelightFrontierIterationsFull, nullptr, include_skylight);
  if (!out_relit_chunks)
  {
    return;
  }
  out_relit_chunks->assign(relit.begin(), relit.end());
}

void ApplyEnterOpenSkyBoundary(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               std::vector<glm::ivec3> *out_changed_chunks,
                               const char *audit_source)
{
  // A missing neighbor is not an open-sky source. Use the same immutable
  // geometry/upper-column solve as completed relights, restricted to sky.
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectColumnChunkCoords(world, world_x, world_z, min_y, max_y, coords);
  const std::vector<glm::ivec3> batch(coords.begin(), coords.end());
  auto changed = RelightChunkCoords(world, registry, batch, false, true);
  if (IsEnvironmentFlagEnabled("CUBATARIUM_RELIGHT_AUDIT"))
    CubatariumLogInfo(
        "RelightAudit",
        std::string("enter_open_sky event=geometry_recompute source=") +
            (audit_source ? audit_source : "unknown") +
            " loaded_slices=" + std::to_string(coords.size()) +
            " changed_chunks=" + std::to_string(changed.size()));
  if (out_changed_chunks)
    *out_changed_chunks = std::move(changed);
}

void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y, bool include_block_light,
                   bool include_skylight)
{
  std::unordered_set<glm::ivec3, IVec3Hash> coords;
  CollectColumnChunkCoords(world, world_x, world_z, min_y, max_y, coords);
  std::vector<glm::ivec3> coord_list(coords.begin(), coords.end());
  if (!coord_list.empty())
  {
    RelightChunkCoords(world, registry, coord_list, include_block_light,
                       include_skylight);
  }
}

void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry)
{
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      { RelightChunk(world, registry, chunk.GetCoord()); });
}

} // namespace cutum
