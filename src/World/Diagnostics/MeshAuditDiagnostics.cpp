#include "World/Diagnostics/MeshAuditDiagnostics.h"

#include "App/Core.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/World.h"
#include "World/Lighting/LightUtil.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Mesh/WorldMeshService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace cutum
{

namespace
{
using json = nlohmann::json;

const char *BlockReasonName(ColumnRenderableState::BlockReason reason)
{
  switch (reason)
  {
  case ColumnRenderableState::BlockReason::None:
    return "none";
  case ColumnRenderableState::BlockReason::PendingLight:
    return "pending_light";
  case ColumnRenderableState::BlockReason::StickyRemesh:
    return "sticky_remesh";
  case ColumnRenderableState::BlockReason::StaleDark:
    return "stale_dark";
  case ColumnRenderableState::BlockReason::MissingMesh:
    return "missing_mesh";
  case ColumnRenderableState::BlockReason::GpuInFlight:
    return "gpu_in_flight";
  case ColumnRenderableState::BlockReason::NotLoaded:
    return "not_loaded";
  case ColumnRenderableState::BlockReason::NotReadyState:
    return "not_ready_state";
  }
  return "unknown";
}

bool IsOpaqueSolid(UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return false;
  }
  const BlockRenderStyle style = registry.GetRenderStyle(id);
  return style != BlockRenderStyle::Cross && style != BlockRenderStyle::Fluid &&
         style != BlockRenderStyle::Cutout && !registry.IsTransparent(id) &&
         registry.BlocksMovement(id);
}
} // namespace

std::filesystem::path UMeshAuditDiagnostics::DefaultPath()
{
  return GetExecutableDirectory() / "mesh_audit.json";
}

int UMeshAuditDiagnostics::Run(UWorld &world,
                              const std::filesystem::path &path_override)
{
  UBlockWorld &block_world = world.GetBlockWorld();
  UBlockRegistry &registry = world.GetBlockRegistry();
  const UWorldMeshService &mesh = world.GetMeshService();

  std::vector<glm::ivec3> coords;
  block_world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        if (!chunk.IsAirOnly())
        {
          coords.push_back(chunk.GetCoord());
        }
      });

  json out;
  out["schema"] = "mesh_audit.v1";
  out["audited_chunks"] = coords.size();
  json bad = json::array();
  json dark = json::array();
  json dark_top = json::array();
  json published_dark_chunks = json::array();
  int total_published_dark = 0;
  int total_dark_top_cells = 0;
  int total_missing = 0;
  int total_expected = 0;
  int total_published_missing = 0;
  long internal_fluid_faces = 0, fluid_quads = 0;
  // Water side faces open to air below sea level: "dams" of water around an
  // air-filled basin, seen as translucent walls from inside the sea.
  long sub_sea_side_walls = 0, sub_sea_side_wall_cells_deep = 0;
  std::map<int, long> sub_sea_wall_by_y;
  std::map<std::string, long> sub_sea_wall_kind;
  json sub_sea_wall_samples = json::array();
  const int audit_sea = world.GetProceduralSettings().SeaLevel;
  std::map<std::string, long> internal_fluid_by_neighbor_kind;
  json internal_fluid_samples = json::array();
  for (const glm::ivec3 &coord : coords)
  {
    const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
    if (!chunk)
    {
      continue;
    }
    // Brute-force expected exposed top faces (+Y) of opaque solids.
    std::set<std::tuple<int, int, int>> expected;
    int unknown_above = 0;
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int y = 0; y < CHUNK_SIZE; ++y)
      {
        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
          const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
          if (!IsOpaqueSolid(registry, id))
          {
            continue;
          }
          const glm::ivec3 above =
              coord * CHUNK_SIZE + glm::ivec3(x, y + 1, z);
          const glm::ivec3 above_chunk = UChunkManager::WorldToChunk(above);
          const BlockId above_id = block_world.GetBlock(above);
          if (!block_world.GetChunkManager().GetChunk(above_chunk))
          {
            if (above_id == BLOCK_AIR)
            {
              expected.emplace(x, y, z); // absent slice above a column = air
            }
            else
            {
              ++unknown_above;
            }
            continue;
          }
          if (above_id == BLOCK_AIR)
          {
            expected.emplace(x, y, z);
          }
        }
      }
    }
    // Exposed top cells whose air-above has zero sky and block light.
    int dark_top_cells = 0;
    json dark_sample = json::array();
    for (const auto &cell : expected)
    {
      const glm::ivec3 above = coord * CHUNK_SIZE +
                               glm::ivec3(std::get<0>(cell), std::get<1>(cell) + 1,
                                          std::get<2>(cell));
      const UChunk *above_chunk = block_world.GetChunkManager().GetChunk(
          UChunkManager::WorldToChunk(above));
      if (!above_chunk)
      {
        continue; // absent slice above terrain: open sky
      }
      const uint8_t packed = above_chunk->GetLightPackedLocal(
          UChunkManager::WorldToLocal(above));
      if (packed == 0)
      {
        bool sky_open = true;
        for (int sy = above.y; sy < 320; ++sy)
        {
          if (block_world.GetBlock(glm::ivec3(above.x, sy, above.z)) != BLOCK_AIR)
          {
            sky_open = false;
            break;
          }
        }
        if (!sky_open)
        {
          continue; // covered (cave / overhang / underwater): dark is legal
        }
        ++dark_top_cells;
        if (dark_sample.size() < 3)
        {
          json s;
          s["world"] = {above.x, above.y - 1, above.z};
          s["block"] = registry.GetTypeNameById(block_world.GetBlock(above - glm::ivec3(0, 1, 0)));
          json up = json::array();
          for (int dy = 0; dy < 4; ++dy)
          {
            up.push_back(registry.GetTypeNameById(
                block_world.GetBlock(above + glm::ivec3(0, dy, 0))));
          }
          s["above_1_4"] = up;
          dark_sample.push_back(s);
        }
      }
    }
    if (dark_top_cells > 0)
    {
      json e;
      e["chunk"] = {coord.x, coord.y, coord.z};
      e["dark_top_cells"] = dark_top_cells;
      e["expected_top_faces"] = expected.size();
      e["samples"] = dark_sample;
      e["light_field_rev"] = chunk->GetLightFieldRevision();
      e["pending_light_slice"] = world.IsPendingLightBeforeMeshSlice(coord);
      e["pending_light_column"] =
          world.IsPendingLightBeforeMesh(glm::ivec2(coord.x, coord.z));
      dark_top.push_back(std::move(e));
      total_dark_top_cells += dark_top_cells;
    }
    // Fresh mesh from the same world state.
    const ChunkMeshSnapshot snapshot = ChunkMeshSnapshot::Capture(
        block_world, coord, /*sourceRevision=*/1, nullptr, nullptr);
    const std::vector<GreedyQuad> quads =
        UGreedyMesher::BuildChunkMesh(snapshot, registry);
    // Fluid faces whose outward neighbour is the same liquid are hidden walls
    // that must not exist: they render as translucent "walls" inside a water
    // body (decor cells carrying their own fluid are the usual cause).
    for (const GreedyQuad &q : quads)
    {
      if (!registry.IsLiquid(q.Id))
      {
        continue;
      }
      ++fluid_quads;
      for (int dv = 0; dv < q.height; ++dv)
      {
        for (int du = 0; du < q.width; ++du)
        {
          glm::ivec3 cell(0);
          if (q.axis == 0)
          {
            cell = glm::ivec3(q.slice, q.u + du, q.v + dv);
          }
          else if (q.axis == 1)
          {
            cell = glm::ivec3(q.v + dv, q.slice, q.u + du);
          }
          else
          {
            cell = glm::ivec3(q.u + du, q.v + dv, q.slice);
          }
          glm::ivec3 outward(0);
          outward[q.axis] = q.faceSign;
          const glm::ivec3 world_cell = coord * CHUNK_SIZE + cell;
          const BlockId neighbour = block_world.GetBlock(world_cell + outward);
          if (neighbour == BLOCK_AIR && q.axis != 1 &&
              world_cell.y < audit_sea - 1)
          {
            ++sub_sea_side_walls;
            ++sub_sea_wall_by_y[world_cell.y];
            {
              const glm::ivec3 air_cell = world_cell + outward;
              const bool water_above = registry.IsLiquid(
                  block_world.GetBlock(air_cell + glm::ivec3(0, 1, 0)));
              const bool water_below = registry.IsLiquid(
                  block_world.GetBlock(air_cell - glm::ivec3(0, 1, 0)));
              ++sub_sea_wall_kind[water_above ? (water_below ? "air_between_water"
                                                             : "air_under_water_ceiling")
                                              : (water_below ? "surface_step"
                                                             : "air_pocket_other")];
            }
            if (sub_sea_wall_samples.size() < 10)
            {
              const glm::ivec3 air = world_cell + outward;
              sub_sea_wall_samples.push_back(
                  {{"water", {world_cell.x, world_cell.y, world_cell.z}},
                   {"air", {air.x, air.y, air.z}},
                   {"air_above_is", registry.GetTypeNameById(block_world.GetBlock(
                                        air + glm::ivec3(0, 1, 0)))},
                   {"air_below_is", registry.GetTypeNameById(block_world.GetBlock(
                                        air - glm::ivec3(0, 1, 0)))}});
            }
          }
          if (neighbour != BLOCK_AIR && registry.IsLiquid(neighbour))
          {
            ++internal_fluid_faces;
            const std::string own = registry.GetTypeNameById(
                block_world.GetBlock(world_cell));
            ++internal_fluid_by_neighbor_kind[own + " -> " +
                                              registry.GetTypeNameById(neighbour)];
            if (internal_fluid_samples.size() < 8)
            {
              internal_fluid_samples.push_back(
                  {{"cell", {world_cell.x, world_cell.y, world_cell.z}},
                   {"axis", q.axis},
                   {"sign", q.faceSign},
                   {"fluid_packed", int(q.FluidPacked)}});
            }
          }
        }
      }
    }
    std::set<std::tuple<int, int, int>> covered;
    for (const GreedyQuad &q : quads)
    {
      if (q.axis != 1 || q.faceSign <= 0)
      {
        continue;
      }
      // axis=1: u axis = z (2), v axis = x (0).
      for (int dv = 0; dv < q.height; ++dv)
      {
        for (int du = 0; du < q.width; ++du)
        {
          covered.emplace(q.v + dv, q.slice, q.u + du);
        }
      }
    }
    // Published CPU mesh (cache): +Y face cells (faceIndex 4).
    std::set<std::tuple<int, int, int>> published;
    std::map<std::tuple<int, int, int>, bool> published_dark;
    bool have_cpu_batches = false;
    if (const auto *batches = mesh.GetCache().DebugGetChunkBatches(coord))
    {
      const glm::vec3 origin(coord * CHUNK_SIZE);
      for (const GreedyMeshBatch &batch : *batches)
      {
        if (batch.Transparent || batch.vertices.empty())
        {
          continue;
        }
        have_cpu_batches = true;
        for (size_t i = 0; i + 3 < batch.vertices.size(); i += 4)
        {
          const GreedyMeshVertex &v0 = batch.vertices[i];
          if (static_cast<int>(v0.faceIndex + 0.5f) != 4)
          {
            continue;
          }
          float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
          for (size_t k = 0; k < 4; ++k)
          {
            const GreedyMeshVertex &v = batch.vertices[i + k];
            minx = std::min(minx, v.px - origin.x);
            maxx = std::max(maxx, v.px - origin.x);
            minz = std::min(minz, v.pz - origin.z);
            maxz = std::max(maxz, v.pz - origin.z);
          }
          const int y = static_cast<int>(std::floor(v0.py - origin.y - 0.5f + 0.5f));
          for (int x = static_cast<int>(std::floor(minx + 0.5f));
               x < static_cast<int>(std::floor(maxx + 0.5f)); ++x)
          {
            for (int z = static_cast<int>(std::floor(minz + 0.5f));
                 z < static_cast<int>(std::floor(maxz + 0.5f)); ++z)
            {
              published.emplace(x, y, z);
              published_dark[std::make_tuple(x, y, z)] =
                  (v0.skyLight <= 0.0f && v0.blockLight <= 0.0f);
            }
          }
        }
      }
    }
    // Published vertex light dark while the light field above is lit.
    int published_dark_but_field_lit = 0;
    json pdark_sample = json::array();
    for (const auto &entry : published_dark)
    {
      if (!entry.second || expected.count(entry.first) == 0)
      {
        continue;
      }
      const auto &c = entry.first;
      const glm::ivec3 above = coord * CHUNK_SIZE +
                               glm::ivec3(std::get<0>(c), std::get<1>(c) + 1, std::get<2>(c));
      const UChunk *ac = block_world.GetChunkManager().GetChunk(
          UChunkManager::WorldToChunk(above));
      const uint8_t packed =
          ac ? ac->GetLightPackedLocal(UChunkManager::WorldToLocal(above)) : 0xFF;
      if (packed != 0)
      {
        ++published_dark_but_field_lit;
        if (pdark_sample.size() < 3)
        {
          pdark_sample.push_back({above.x, above.y - 1, above.z, packed});
        }
      }
    }
    if (published_dark_but_field_lit > 0)
    {
      json e;
      e["chunk"] = {coord.x, coord.y, coord.z};
      e["cells"] = published_dark_but_field_lit;
      e["samples"] = pdark_sample;
      e["meshed_light_rev"] = mesh.GetCache().GetMeshedLightRevision(coord);
      e["light_field_rev"] = chunk->GetLightFieldRevision();
      e["preview"] = mesh.GetCache().HasProvisionalLightPreview(coord);
      e["draws_preview"] = world.ShouldDrawProvisionalLightPreview(coord);
      e["mesh_dirty"] = mesh.IsChunkMeshDirty(coord);
      published_dark_chunks.push_back(std::move(e));
      total_published_dark += published_dark_but_field_lit;
    }
    int published_missing = 0;
    json published_samples = json::array();
    if (have_cpu_batches)
    {
      for (const auto &cell : expected)
      {
        if (published.count(cell) == 0)
        {
          ++published_missing;
          if (published_samples.size() < 6)
          {
            published_samples.push_back({std::get<0>(cell), std::get<1>(cell),
                                         std::get<2>(cell)});
          }
        }
      }
    }
    total_published_missing += published_missing;
    int missing = 0;
    json samples = json::array();
    for (const auto &cell : expected)
    {
      if (covered.count(cell) == 0)
      {
        ++missing;
        if (samples.size() < 6)
        {
          samples.push_back({std::get<0>(cell), std::get<1>(cell),
                             std::get<2>(cell)});
        }
      }
    }
    total_missing += missing;
    total_expected += static_cast<int>(expected.size());
    const bool drawable = mesh.HasDrawableGreedyMesh(coord);
    const bool has_mesh = mesh.HasGreedyMesh(coord);
    if (drawable && mesh.GetCache().ChunkHasFullyDarkFace(coord))
    {
      const ColumnRenderableState col =
          world.GetColumnRenderableState(glm::ivec2(coord.x, coord.z));
      json d;
      d["chunk"] = {coord.x, coord.y, coord.z};
      d["lit_drawable_face"] = mesh.ChunkHasLitDrawableFace(coord);
      d["provisional_preview"] = mesh.GetCache().HasProvisionalLightPreview(coord);
      d["draws_preview"] = world.ShouldDrawProvisionalLightPreview(coord);
      d["pending_light_slice"] = world.IsPendingLightBeforeMeshSlice(coord);
      d["pending_light_column"] =
          world.IsPendingLightBeforeMesh(glm::ivec2(coord.x, coord.z));
      d["light_field_rev"] = chunk->GetLightFieldRevision();
      d["meshed_light_rev"] = mesh.GetCache().GetMeshedLightRevision(coord);
      d["column_reason"] = BlockReasonName(col.reason);
      d["column_ticket"] = col.has_repair_ticket;
      d["mesh_dirty"] = mesh.IsChunkMeshDirty(coord);
      dark.push_back(std::move(d));
    }
    if (missing > 0 || published_missing > 0 ||
        (!expected.empty() && !drawable))
    {
      json entry;
      entry["chunk"] = {coord.x, coord.y, coord.z};
      entry["expected_top_faces"] = expected.size();
      entry["mesher_missing_top_faces"] = missing;
      entry["sample_missing_local_xyz"] = samples;
      entry["published_cpu_missing_top_faces"] = published_missing;
      entry["published_sample_missing_local_xyz"] = published_samples;
      entry["has_cpu_batches"] = have_cpu_batches;
      {
        json col = json::array();
        for (int y = 0; y < 10; ++y)
        {
          const BlockId id = chunk->GetBlockLocal(glm::ivec3(8, y, 8));
          col.push_back({id, registry.GetTypeNameById(id)});
        }
        entry["column_8_y_0_9"] = col;
        entry["chunk_above_resident"] =
            block_world.GetChunkManager().GetChunk(coord + glm::ivec3(0, 1, 0)) != nullptr;
        entry["chunk_below_resident"] =
            block_world.GetChunkManager().GetChunk(coord - glm::ivec3(0, 1, 0)) != nullptr;
      }
      {
        const MeshPublishRevs revs = mesh.GetCache().GetMeshPublishRevs(coord);
        entry["revs"] = {{"content", chunk->GetContentRevision()},
                         {"light_field", chunk->GetLightFieldRevision()},
                         {"incarnation", chunk->GetIncarnation()},
                         {"pub_geom", revs.geom_rev},
                         {"pub_light", revs.light_rev},
                         {"chunk_mesh_rev", mesh.GetChunkMeshRevision(coord)}};
      }
      {
        json ey = json::object();
        for (const auto &cell : expected)
        {
          const std::string key = std::to_string(std::get<1>(cell));
          ey[key] = ey.value(key, 0) + 1;
        }
        json py = json::object();
        for (const auto &cell : published)
        {
          const std::string key = std::to_string(std::get<1>(cell));
          py[key] = py.value(key, 0) + 1;
        }
        entry["expected_by_local_y"] = ey;
        entry["published_by_local_y"] = py;
      }
      {
        json hist = json::object();
        size_t verts = 0;
        int batch_n = 0;
        if (const auto *bs = mesh.GetCache().DebugGetChunkBatches(coord))
        {
          for (const GreedyMeshBatch &b : *bs)
          {
            ++batch_n;
            verts += b.vertices.size();
            for (size_t vi = 0; vi < b.vertices.size(); vi += 4)
            {
              const std::string key =
                  std::string(b.Transparent ? "t" : "o") +
                  std::to_string(static_cast<int>(b.vertices[vi].faceIndex + 0.5f));
              hist[key] = hist.value(key, 0) + 1;
            }
          }
        }
        if (const auto *bs2 = mesh.GetCache().DebugGetChunkBatches(coord))
        {
          json vs = json::array();
          for (const GreedyMeshBatch &b : *bs2)
          {
            for (size_t vi = 0; vi < b.vertices.size() && vs.size() < 8; ++vi)
            {
              const auto &v = b.vertices[vi];
              vs.push_back({v.px, v.py, v.pz, v.faceIndex});
            }
          }
          entry["cpu_first_vertices"] = vs;
          entry["chunk_origin"] = {coord.x * CHUNK_SIZE, coord.y * CHUNK_SIZE,
                                   coord.z * CHUNK_SIZE};
        }
        entry["cpu_batch_count"] = batch_n;
        entry["cpu_vertex_count"] = verts;
        entry["cpu_quads_by_face"] = hist;
        entry["meshed_light_revision"] =
            mesh.GetCache().GetMeshedLightRevision(coord);
        entry["defer_mesh_until_lit"] =
            mesh.GetCache().IsDeferMeshUntilLit(coord);
        entry["provisional_preview"] =
            mesh.GetCache().HasProvisionalLightPreview(coord);
      }
      entry["unknown_above_cells"] = unknown_above;
      entry["has_greedy_mesh"] = has_mesh;
      entry["drawable_greedy_mesh"] = drawable;
      entry["gpu_resident"] = mesh.GetCache().QueryGreedyGpuResident(coord);
      entry["mesh_dirty"] = mesh.IsChunkMeshDirty(coord);
      entry["async_inflight"] = mesh.HasInflightMeshBuild(coord);
      entry["softdefer_held"] = mesh.IsSoftDeferHeld(coord);
      entry["pending_light"] = world.IsPendingLightBeforeMeshSlice(coord);
      entry["slice_render_ready"] = world.IsChunkSliceRenderReady(coord);
      bad.push_back(std::move(entry));
    }
  }
  out["total_expected_top_faces"] = total_expected;
  out["total_mesher_missing_top_faces"] = total_missing;
  out["total_published_cpu_missing_top_faces"] = total_published_missing;
  out["reconcile_redirty_total"] = mesh.GetCache().GetReconcileRedirtyCount();
  // Waterlogged decor audit: permeable (plant) cells carrying fluid, and whether
  // any face of such a cell is exposed to air (reads as a water cube with the
  // plant inside, especially above the sea surface).
  {
    const int sea = world.GetProceduralSettings().SeaLevel;
    long total_waterlogged = 0, exposed_above_sea = 0, exposed_below_sea = 0,
         above_sea = 0;
    std::map<std::string, int> by_type;
    json wl_samples = json::array();
    static const glm::ivec3 kOff[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                       {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (const glm::ivec3 &coord : coords)
    {
      const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
      if (!chunk)
      {
        continue;
      }
      for (int z = 0; z < CHUNK_SIZE; ++z)
      {
        for (int y = 0; y < CHUNK_SIZE; ++y)
        {
          for (int x = 0; x < CHUNK_SIZE; ++x)
          {
            const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
            if (id == BLOCK_AIR || registry.IsLiquid(id) ||
                !registry.IsFluidPermeable(id))
            {
              continue;
            }
            const glm::ivec3 wp = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
            if (PackFluidCellState(block_world.GetFluidState(wp)) == 0)
            {
              continue;
            }
            ++total_waterlogged;
            const bool over = wp.y > sea;
            above_sea += over ? 1 : 0;
            bool exposed = false;
            for (const glm::ivec3 &o : kOff)
            {
              const glm::ivec3 np = wp + o;
              if (block_world.GetBlock(np) == BLOCK_AIR &&
                  PackFluidCellState(block_world.GetFluidState(np)) == 0)
              {
                exposed = true;
                break;
              }
            }
            if (exposed)
            {
              (over ? exposed_above_sea : exposed_below_sea) += 1;
              const std::string name = registry.GetTypeNameById(id);
              by_type[name + (over ? "@above_sea" : "@below_sea")] += 1;
              if (wl_samples.size() < 8)
              {
                wl_samples.push_back({{"pos", {wp.x, wp.y, wp.z}},
                                      {"type", name}});
              }
            }
          }
        }
      }
    }
    // Water body consistency: submerged non-source cells and water blocks with
    // no fluid state read as cubes inside the body.
    {
      long water_cells = 0, source_cells = 0, flowing_cells = 0,
           zero_state_cells = 0, submerged_nonsource = 0, falling_cells = 0;
      json ws = json::array();
      for (const glm::ivec3 &coord : coords)
      {
        const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
        if (!chunk)
        {
          continue;
        }
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
          for (int y = 0; y < CHUNK_SIZE; ++y)
          {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
              if (id == BLOCK_AIR || !registry.IsLiquid(id))
              {
                continue;
              }
              const glm::ivec3 wp = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
              const FluidCellState st = block_world.GetFluidState(wp);
              ++water_cells;
              if (PackFluidCellState(st) == 0)
              {
                ++zero_state_cells;
                continue;
              }
              if (st.IsSource())
              {
                ++source_cells;
                continue;
              }
              ++flowing_cells;
              falling_cells += st.Falling ? 1 : 0;
              const glm::ivec3 up = wp + glm::ivec3(0, 1, 0);
              if (registry.IsLiquid(block_world.GetBlock(up)))
              {
                ++submerged_nonsource;
                if (ws.size() < 8)
                {
                  ws.push_back({{"pos", {wp.x, wp.y, wp.z}},
                                {"level", int(st.Level)},
                                {"falling", int(st.Falling)}});
                }
              }
            }
          }
        }
      }
      // Surface steps: a water cell with air above whose horizontal neighbour is
      // air over a lower water cell (side wall of the higher surface is visible).
      long surface_cells = 0, step_cells = 0, pillar_cells = 0;
      json step_samples = json::array();
      for (const glm::ivec3 &coord : coords)
      {
        const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
        if (!chunk)
        {
          continue;
        }
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
          for (int y = 0; y < CHUNK_SIZE; ++y)
          {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
              if (id == BLOCK_AIR || !registry.IsLiquid(id))
              {
                continue;
              }
              const glm::ivec3 wp = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
              if (block_world.GetBlock(wp + glm::ivec3(0, 1, 0)) != BLOCK_AIR)
              {
                continue;
              }
              ++surface_cells;
              static const glm::ivec3 kH[4] = {
                  {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
              int lower_water_neighbors = 0;
              for (const glm::ivec3 &o : kH)
              {
                const glm::ivec3 n = wp + o;
                if (block_world.GetBlock(n) == BLOCK_AIR &&
                    registry.IsLiquid(block_world.GetBlock(n - glm::ivec3(0, 1, 0))))
                {
                  ++lower_water_neighbors;
                }
              }
              if (lower_water_neighbors > 0)
              {
                ++step_cells;
                if (lower_water_neighbors >= 3)
                {
                  ++pillar_cells;
                  if (step_samples.size() < 8)
                  {
                    step_samples.push_back({wp.x, wp.y, wp.z});
                  }
                }
              }
            }
          }
        }
      }
      out["liquid_surface_cells"] = surface_cells;
      out["liquid_surface_step_cells"] = step_cells;
      out["liquid_surface_pillar_cells"] = pillar_cells;
      out["liquid_surface_pillar_samples"] = step_samples;
      out["liquid_cells"] = water_cells;
      out["liquid_source_cells"] = source_cells;
      out["liquid_flowing_cells"] = flowing_cells;
      out["liquid_falling_cells"] = falling_cells;
      out["liquid_zero_state_cells"] = zero_state_cells;
      out["liquid_submerged_nonsource"] = submerged_nonsource;
      out["liquid_submerged_nonsource_samples"] = ws;
    }
    // Sky light just under the water surface (open sky above): zero there means
    // the back side of the surface is rendered black (light not ready).
    {
      std::map<int, std::pair<long, long>> by_depth; // depth -> {cells, sky==0}
      std::map<std::string, long> zero_kind;
      json zero_samples = json::array();
      json zero_profiles = json::array();
      for (const glm::ivec3 &coord : coords)
      {
        const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
        if (!chunk)
        {
          continue;
        }
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
          for (int y = 0; y < CHUNK_SIZE; ++y)
          {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
              const BlockId id = chunk->GetBlockLocal(glm::ivec3(x, y, z));
              if (id == BLOCK_AIR || !registry.IsLiquid(id))
              {
                continue;
              }
              const glm::ivec3 wp = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
              if (block_world.GetBlock(wp + glm::ivec3(0, 1, 0)) != BLOCK_AIR)
              {
                continue; // not a surface cell
              }
              // open sky above the surface?
              bool open = true;
              for (int sy = wp.y + 1; sy < wp.y + 24; ++sy)
              {
                if (block_world.GetBlock(glm::ivec3(wp.x, sy, wp.z)) != BLOCK_AIR)
                {
                  open = false;
                  break;
                }
              }
              if (!open)
              {
                continue;
              }
              for (int d = 0; d <= 3; ++d)
              {
                const glm::ivec3 cell = wp - glm::ivec3(0, d, 0);
                if (!registry.IsLiquid(block_world.GetBlock(cell)))
                {
                  break;
                }
                const UChunk *cc = block_world.GetChunkManager().GetChunk(
                    UChunkManager::WorldToChunk(cell));
                if (!cc)
                {
                  break;
                }
                const uint8_t packed =
                    cc->GetLightPackedLocal(UChunkManager::WorldToLocal(cell));
                int cam_dist_chunks = 99;
                if (const auto cam = world.GetCurrentUserCamera())
                {
                  const glm::ivec3 cam_chunk = UChunkManager::WorldToChunk(
                      glm::ivec3(glm::floor(cam->GetPosition())));
                  const glm::ivec3 cell_chunk = UChunkManager::WorldToChunk(cell);
                  cam_dist_chunks = std::max(std::abs(cell_chunk.x - cam_chunk.x),
                                             std::abs(cell_chunk.z - cam_chunk.z));
                }
                if (cam_dist_chunks > 4)
                {
                  break; // outside the visible radius (keep shell)
                }
                auto &slot = by_depth[d];
                ++slot.first;
                if (UnpackSky(packed) == 0)
                {
                  ++slot.second;
                  if (d == 0)
                  {
                    const glm::ivec3 cc_coord = UChunkManager::WorldToChunk(cell);
                    const bool never_lit = cc->GetLightFieldRevision() <= 1;
                    const bool pending = world.IsPendingLightBeforeMesh(
                        glm::ivec2(cc_coord.x, cc_coord.z));
                    ++zero_kind[std::string(never_lit ? "never_lit" : "lit_once") +
                                (pending ? "+pending" : "+settled")];
                    if (zero_samples.size() < 8)
                    {
                      json profile = json::array();
                      for (int dy = 1; dy <= 20; dy += 1)
                      {
                        const glm::ivec3 pc = cell + glm::ivec3(0, dy, 0);
                        const UChunk *pch = block_world.GetChunkManager().GetChunk(
                            UChunkManager::WorldToChunk(pc));
                        profile.push_back(pch ? UnpackSky(pch->GetLightPackedLocal(
                                                   UChunkManager::WorldToLocal(pc)))
                                              : -1);
                      }
                      json above_chunks = json::array();
                      for (int k = 1; k <= 3; ++k)
                      {
                        const UChunk *ac = block_world.GetChunkManager().GetChunk(
                            cc_coord + glm::ivec3(0, k, 0));
                        above_chunks.push_back(ac ? int(ac->GetLightFieldRevision())
                                                  : -1);
                      }
                      zero_profiles.push_back({{"cell", {cell.x, cell.y, cell.z}},
                                               {"sky_up_1_20", profile},
                                               {"light_rev_chunks_above", above_chunks}});
                      zero_samples.push_back(
                          {{"cell", {cell.x, cell.y, cell.z}},
                           {"light_rev", cc->GetLightFieldRevision()},
                           {"neighbors_sky",
                            {UnpackSky(cc->GetLightPackedLocal(
                                 UChunkManager::WorldToLocal(cell + glm::ivec3(1, 0, 0)) )),
                             UnpackSky(cc->GetLightPackedLocal(
                                 UChunkManager::WorldToLocal(cell + glm::ivec3(0, 1, 0)) ))}}});
                    }
                  }
                }
              }
            }
          }
        }
      }
      json depth_json = json::object();
      for (const auto &entry : by_depth)
      {
        depth_json[std::to_string(entry.first)] = {{"cells", entry.second.first},
                                                   {"sky_zero", entry.second.second}};
      }
      out["water_surface_sky_light_by_depth"] = depth_json;
      out["water_surface_sky_zero_kind"] = zero_kind;
      out["water_surface_sky_zero_samples"] = zero_samples;
      out["water_surface_sky_zero_profiles"] = zero_profiles;
    }
    // Lily pad placement: it must rest on the water surface (water below,
    // air above, no fluid in its own cell).
    {
      const BlockId pad = registry.GetIdByTypeName("lily_pad");
      long pads = 0, ok = 0, over_land = 0, over_air = 0, submerged = 0,
           fluid_in_cell = 0, water_below_not_source = 0;
      std::map<int, int> height_over_sea;
      json bad_samples = json::array();
      if (pad != BLOCK_AIR)
      {
        for (const glm::ivec3 &coord : coords)
        {
          const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord);
          if (!chunk)
          {
            continue;
          }
          for (int z = 0; z < CHUNK_SIZE; ++z)
          {
            for (int y = 0; y < CHUNK_SIZE; ++y)
            {
              for (int x = 0; x < CHUNK_SIZE; ++x)
              {
                if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != pad)
                {
                  continue;
                }
                const glm::ivec3 wp = coord * CHUNK_SIZE + glm::ivec3(x, y, z);
                ++pads;
                ++height_over_sea[wp.y - sea];
                const BlockId below = block_world.GetBlock(wp - glm::ivec3(0, 1, 0));
                const BlockId above = block_world.GetBlock(wp + glm::ivec3(0, 1, 0));
                const bool water_below = registry.IsLiquid(below);
                const bool liquid_above = registry.IsLiquid(above);
                const bool fluid_here =
                    PackFluidCellState(block_world.GetFluidState(wp)) != 0;
                bool good = true;
                if (!water_below)
                {
                  ++(below == BLOCK_AIR ? over_air : over_land);
                  good = false;
                }
                else
                {
                  const FluidCellState below_state =
                      block_world.GetFluidState(wp - glm::ivec3(0, 1, 0));
                  if (!below_state.IsSource())
                  {
                    ++water_below_not_source;
                  }
                }
                if (liquid_above)
                {
                  ++submerged;
                  good = false;
                }
                if (fluid_here)
                {
                  ++fluid_in_cell;
                  good = false;
                }
                if (good)
                {
                  ++ok;
                }
                else if (bad_samples.size() < 8)
                {
                  bad_samples.push_back({{"pos", {wp.x, wp.y, wp.z}},
                                         {"below", registry.GetTypeNameById(below)},
                                         {"above", registry.GetTypeNameById(above)}});
                }
              }
            }
          }
        }
      }
      out["lily_pads"] = {{"total", pads},
                          {"ok_on_surface", ok},
                          {"over_land", over_land},
                          {"over_air", over_air},
                          {"submerged", submerged},
                          {"fluid_in_cell", fluid_in_cell},
                          {"water_below_not_source", water_below_not_source},
                          {"height_over_sea", height_over_sea},
                          {"bad_samples", bad_samples}};
    }
    out["waterlogged_total"] = total_waterlogged;
    out["waterlogged_above_sea"] = above_sea;
    out["waterlogged_exposed_above_sea"] = exposed_above_sea;
    out["waterlogged_exposed_below_sea"] = exposed_below_sea;
    out["waterlogged_exposed_by_type"] = by_type;
    out["waterlogged_exposed_samples"] = wl_samples;
    out["sea_level"] = sea;
  }
  out["sub_sea_water_side_walls"] = sub_sea_side_walls;
  out["sub_sea_water_side_walls_by_y"] = sub_sea_wall_by_y;
  out["sub_sea_water_side_walls_by_kind"] = sub_sea_wall_kind;
  out["sub_sea_water_side_wall_samples"] = sub_sea_wall_samples;
  out["fluid_quads"] = fluid_quads;
  out["internal_fluid_faces"] = internal_fluid_faces;
  out["internal_fluid_faces_by_kind"] = internal_fluid_by_neighbor_kind;
  out["internal_fluid_face_samples"] = internal_fluid_samples;
  out["suspect_chunks"] = bad;
  out["dark_drawable_chunks"] = dark;
  out["dark_top_chunks"] = dark_top;
  out["published_dark_field_lit_chunks"] = published_dark_chunks;
  out["total_published_dark_field_lit_cells"] = total_published_dark;
  out["total_dark_top_cells"] = total_dark_top_cells;
  std::ofstream file(path_override.empty() ? DefaultPath() : path_override,
                     std::ios::trunc);
  if (!file)
  {
    return -1;
  }
  file << out.dump(1) << '\n';
  return static_cast<int>(coords.size());
}

} // namespace cutum

namespace cutum
{

void GetLightPreviewDebounceCounters(long long &raw_toggles,
                                     long long &shown_toggles);

namespace
{
struct FlickerChunkState
{
  bool seen{false};
  bool preview{false};
  bool ready{false};
  bool dark{false};
  uint64_t meshed_light_rev{0};
  uint64_t pub_geom_rev{0};
  uint64_t field_light_rev{0};
  int preview_toggles{0};
  int ready_toggles{0};
  int dark_toggles{0};
  int light_rev_changes{0};
  int geom_rev_changes{0};
  int field_rev_changes{0};
  int frames{0};
};
std::map<std::tuple<int, int, int>, FlickerChunkState> gFlicker;
uint64_t gFlickerFrames = 0;

struct EditTrack
{
  uint64_t last_content{0};
  bool pending{false};
  uint64_t pending_rev{0};
  double start_ms{0.0};
  bool stuck_reported{false};
};
std::map<std::tuple<int, int, int>, EditTrack> gEditTracks;
std::vector<double> gEditLatenciesMs;
int gEditPublishState[4] = {0, 0, 0, 0}; // bit1=gpu_resident bit0=cpu_batches
nlohmann::json gStuckEdits = nlohmann::json::array();
double NowMs()
{
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - t0)
      .count();
}
} // namespace

void UMeshAuditDiagnostics::TraceFlickerFrame(UWorld &world)
{
  ++gFlickerFrames;
  const auto camera = world.GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::vec3 pos = camera->GetPosition();
  const glm::ivec3 cam_chunk(static_cast<int>(std::floor(pos.x / CHUNK_SIZE)),
                             static_cast<int>(std::floor(pos.y / CHUNK_SIZE)),
                             static_cast<int>(std::floor(pos.z / CHUNK_SIZE)));
  const UWorldMeshService &mesh = world.GetMeshService();
  {
    // Edit visibility: a content change of a chunk that already has a published
    // mesh must reach a newly published mesh quickly. Track the delay and dump
    // the state of edits that stay unpublished.
    const double now_ms = NowMs();
    for (int dx = -4; dx <= 4; ++dx)
    {
      for (int dz = -4; dz <= 4; ++dz)
      {
        for (int dy = -2; dy <= 2; ++dy)
        {
          const glm::ivec3 coord = cam_chunk + glm::ivec3(dx, dy, dz);
          const UChunk *chunk =
              world.GetBlockWorld().GetChunkManager().GetChunk(coord);
          if (!chunk)
          {
            continue;
          }
          const uint64_t published = mesh.GetCache().GetDrawnCenterContentRev(coord);
          EditTrack &t = gEditTracks[std::make_tuple(coord.x, coord.y, coord.z)];
          const uint64_t content = chunk->GetContentRevision();
          if (t.last_content != 0 && content != t.last_content && published != 0)
          {
            if (!t.pending)
            {
              t.pending = true;
              t.start_ms = now_ms;
              t.stuck_reported = false;
            }
            t.pending_rev = content;
          }
          t.last_content = content;
          if (t.pending && published >= t.pending_rev)
          {
            gEditLatenciesMs.push_back(now_ms - t.start_ms);
            t.pending = false;
            {
              const bool gpu = mesh.GetCache().QueryGreedyGpuResident(coord);
              const auto *batches = mesh.GetCache().DebugGetChunkBatches(coord);
              const bool cpu = batches && !batches->empty();
              ++gEditPublishState[(gpu ? 2 : 0) + (cpu ? 1 : 0)];
            }
          }
          else if (t.pending && !t.stuck_reported && now_ms - t.start_ms > 2000.0)
          {
            t.stuck_reported = true;
            nlohmann::json e;
            e["chunk"] = {coord.x, coord.y, coord.z};
            e["pending_content_rev"] = t.pending_rev;
            e["drawn_content_rev"] = published;
            e["published_cpu_content_rev"] = mesh.GetCache().GetPublishedCenterContentRev(coord);
            e["age_ms"] = now_ms - t.start_ms;
            e["drawable"] = mesh.HasDrawableGreedyMesh(coord);
            e["gpu_resident"] = mesh.GetCache().QueryGreedyGpuResident(coord);
            e["dirty"] = mesh.IsChunkMeshDirty(coord);
            e["inflight"] = mesh.HasInflightMeshBuild(coord);
            e["pending_gpu_apply"] = mesh.IsPendingGpuApply(coord);
            e["gpu_extract_in_flight"] = mesh.IsGpuExtractInFlight(coord);
            e["remesh_after_apply"] = mesh.IsRemeshAfterApplyPending(coord);
            e["softdefer_held"] = mesh.IsSoftDeferHeld(coord);
            e["defer_until_lit"] = mesh.GetCache().IsDeferMeshUntilLit(coord);
            e["pending_light_slice"] = world.IsPendingLightBeforeMeshSlice(coord);
            e["slice_render_ready"] = world.IsChunkSliceRenderReady(coord);
            gStuckEdits.push_back(std::move(e));
          }
        }
      }
    }
  }
  constexpr int kRadius = 6;
  for (int dx = -kRadius; dx <= kRadius; ++dx)
  {
    for (int dz = -kRadius; dz <= kRadius; ++dz)
    {
      for (int dy = -2; dy <= 2; ++dy)
      {
        const glm::ivec3 coord = cam_chunk + glm::ivec3(dx, dy, dz);
        if (!mesh.HasDrawableGreedyMesh(coord))
        {
          continue;
        }
        const UChunk *chunk = world.GetBlockWorld().GetChunkManager().GetChunk(coord);
        if (!chunk)
        {
          continue;
        }
        FlickerChunkState &s = gFlicker[std::make_tuple(coord.x, coord.y, coord.z)];
        const bool preview = world.ShouldDrawProvisionalLightPreview(coord);
        const bool ready = world.IsChunkSliceRenderReady(coord);
        const bool dark = mesh.GetCache().ChunkHasFullyDarkFace(coord) &&
                          !mesh.ChunkHasLitDrawableFace(coord);
        const uint64_t lrev = mesh.GetCache().GetMeshedLightRevision(coord);
        const uint64_t grev = mesh.GetCache().GetPublishCount(coord);
        const uint64_t frev = chunk->GetLightFieldRevision();
        if (s.seen)
        {
          s.preview_toggles += preview != s.preview;
          s.ready_toggles += ready != s.ready;
          s.dark_toggles += dark != s.dark;
          s.light_rev_changes += lrev != s.meshed_light_rev;
          s.geom_rev_changes += grev != s.pub_geom_rev;
          s.field_rev_changes += frev != s.field_light_rev;
        }
        s.seen = true;
        s.preview = preview;
        s.ready = ready;
        s.dark = dark;
        s.meshed_light_rev = lrev;
        s.pub_geom_rev = grev;
        s.field_light_rev = frev;
        ++s.frames;
      }
    }
  }
}

void UMeshAuditDiagnostics::FinishFlickerTrace()
{
  static bool done = false;
  if (done || gFlicker.empty())
  {
    return;
  }
  done = true;
  using json = nlohmann::json;
  json out;
  out["frames"] = gFlickerFrames;
  long long preview = 0, ready = 0, dark = 0, light = 0, geom = 0, field = 0;
  std::vector<std::pair<int, const std::pair<const std::tuple<int, int, int>, FlickerChunkState> *>> ranked;
  for (const auto &entry : gFlicker)
  {
    const FlickerChunkState &s = entry.second;
    preview += s.preview_toggles;
    ready += s.ready_toggles;
    dark += s.dark_toggles;
    light += s.light_rev_changes;
    geom += s.geom_rev_changes;
    field += s.field_rev_changes;
    ranked.emplace_back(s.preview_toggles + s.ready_toggles + s.dark_toggles +
                            s.light_rev_changes + s.geom_rev_changes,
                        &entry);
  }
  {
    long long raw_t = 0, shown_t = 0;
    GetLightPreviewDebounceCounters(raw_t, shown_t);
    out["preview_raw_toggles_in_render"] = raw_t;
    out["preview_shown_toggles_in_render"] = shown_t;
  }
  out["chunks_tracked"] = gFlicker.size();
  out["totals"] = {{"preview_toggles", preview}, {"ready_toggles", ready},
                   {"dark_toggles", dark},       {"meshed_light_rev_changes", light},
                   {"geom_rev_changes", geom},   {"field_light_rev_changes", field}};
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });
  json top = json::array();
  for (size_t i = 0; i < ranked.size() && i < 20; ++i)
  {
    const auto &key = ranked[i].second->first;
    const FlickerChunkState &s = ranked[i].second->second;
    top.push_back({{"chunk", {std::get<0>(key), std::get<1>(key), std::get<2>(key)}},
                   {"frames", s.frames},
                   {"preview_toggles", s.preview_toggles},
                   {"ready_toggles", s.ready_toggles},
                   {"dark_toggles", s.dark_toggles},
                   {"meshed_light_rev_changes", s.light_rev_changes},
                   {"geom_rev_changes", s.geom_rev_changes},
                   {"field_light_rev_changes", s.field_rev_changes}});
  }
  out["top_chunks"] = top;
  {
    std::vector<double> lat = gEditLatenciesMs;
    std::sort(lat.begin(), lat.end());
    json l;
    l["count"] = lat.size();
    if (!lat.empty())
    {
      l["median_ms"] = lat[lat.size() / 2];
      l["p90_ms"] = lat[lat.size() * 9 / 10];
      l["max_ms"] = lat.back();
    }
    l["state_cpu_only"] = gEditPublishState[1];
    l["state_gpu_only"] = gEditPublishState[2];
    l["state_gpu_and_cpu"] = gEditPublishState[3];
    l["state_neither"] = gEditPublishState[0];
    out["edit_publish_latency"] = l;
    out["stuck_edits"] = gStuckEdits;
    json unresolved = json::array();
    const double end_ms = NowMs();
    for (const auto &entry : gEditTracks)
    {
      if (entry.second.pending && end_ms - entry.second.start_ms > 2000.0)
      {
        unresolved.push_back({{"chunk", {std::get<0>(entry.first), std::get<1>(entry.first), std::get<2>(entry.first)}},
                              {"age_ms", end_ms - entry.second.start_ms}});
      }
    }
    out["unresolved_edits_at_end"] = unresolved;
  }
  std::ofstream file(GetExecutableDirectory() / "light_flicker_trace.json",
                     std::ios::trunc);
  file << out.dump(1) << '\n';
}

} // namespace cutum

namespace cutum
{

void UMeshAuditDiagnostics::TraceFlightTelemetry(UWorld &world)
{
  using json = nlohmann::json;
  static std::ofstream file;
  static bool opened = false;
  static auto last_emit = std::chrono::steady_clock::now();
  static auto last_frame = std::chrono::steady_clock::now();
  static glm::vec3 last_pos(0.0f);
  static bool have_last_pos = false;
  static double frame_ms_sum = 0.0;
  static double frame_ms_max = 0.0;
  static int frame_n = 0;
  static long st_candidates = 0, st_ring_blocked = 0, st_async_queued = 0,
              st_loads = 0, st_near_skipped = 0;
  static int st_pending_max = 0, st_inflight_max = 0, st_ready_max = 0;
  static float fog_prev = -1.0f, fog_prev_start = -1.0f;
  static double fog_tv = 0.0, fog_max_rate = 0.0, fog_max_accel = 0.0;
  static double fog_prev_rate = 0.0;
  static float fog_min = 1e9f, fog_max = -1e9f;
  if (const StreamingFrameStats *st = world.GetStreamingService()
                                          ? world.GetStreamingService()->GetLastFrameStats()
                                          : nullptr)
  {
    st_candidates += st->loadCandidates;
    st_ring_blocked += st->ringGateBlocked;
    st_async_queued += st->asyncQueuedThisFrame;
    st_loads += st->loadsThisFrame;
    st_near_skipped += st->nearLoadSkipped;
  }
  if (const UChunkLoadScheduler *sched =
          world.GetStreamingService() ? world.GetStreamingService()->GetChunkScheduler() : nullptr)
  {
    st_pending_max = std::max(st_pending_max, sched->GetPendingQueueCount());
    st_inflight_max = std::max(st_inflight_max, sched->GetGenInFlightCount());
    st_ready_max = std::max(st_ready_max, sched->GetCompletedReadyCount());
  }
  const auto now = std::chrono::steady_clock::now();
  {
    const float fog_h = world.GetPresentedFogHorizonBlocks();
    const double dt_s =
        std::chrono::duration<double>(now - last_frame).count();
    if (fog_h >= 0.0f)
    {
      fog_min = std::min(fog_min, fog_h);
      fog_max = std::max(fog_max, fog_h);
      if (fog_prev >= 0.0f && dt_s > 1e-4)
      {
        const double rate = (fog_h - fog_prev) / dt_s;
        fog_tv += std::fabs(fog_h - fog_prev);
        fog_max_rate = std::max(fog_max_rate, std::fabs(rate));
        fog_max_accel = std::max(fog_max_accel,
                                 std::fabs(rate - fog_prev_rate) / dt_s);
        fog_prev_rate = rate;
      }
      fog_prev = fog_h;
    }
  }
  const double frame_ms =
      std::chrono::duration<double, std::milli>(now - last_frame).count();
  last_frame = now;
  frame_ms_sum += frame_ms;
  frame_ms_max = std::max(frame_ms_max, frame_ms);
  ++frame_n;
  const auto camera = world.GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const double since =
      std::chrono::duration<double>(now - last_emit).count();
  if (since < 2.0)
  {
    return;
  }
  if (!opened)
  {
    file.open(GetExecutableDirectory() / "flight_telemetry.jsonl",
              std::ios::trunc);
    opened = true;
  }
  const glm::vec3 pos = camera->GetPosition();
  const glm::vec3 front = camera->GetFront();
  glm::vec2 dir(front.x, front.z);
  const float dir_len = glm::length(dir);
  dir = dir_len > 1e-4f ? dir / dir_len : glm::vec2(0.0f, 1.0f);
  const double speed =
      have_last_pos ? glm::length(glm::vec2(pos.x - last_pos.x, pos.z - last_pos.z)) / since
                    : 0.0;
  last_pos = pos;
  have_last_pos = true;
  const UWorldMeshService &mesh = world.GetMeshService();
  const UChunkManager &chunks = world.GetBlockWorld().GetChunkManager();
  const int cam_cy = static_cast<int>(std::floor(pos.y / CHUNK_SIZE));
  auto column_state = [&](int cx, int cz, bool &resident, bool &drawable)
  {
    resident = false;
    drawable = false;
    for (int cy = std::max(-4, cam_cy - 40); cy <= cam_cy + 1; ++cy)
    {
      const glm::ivec3 c(cx, cy, cz);
      if (chunks.GetChunk(c))
      {
        resident = true;
        if (mesh.HasDrawableGreedyMesh(c))
        {
          drawable = true;
          return;
        }
      }
    }
  };
  const int cam_cx = static_cast<int>(std::floor(pos.x / CHUNK_SIZE));
  const int cam_cz = static_cast<int>(std::floor(pos.z / CHUNK_SIZE));
  int first_missing_resident = -1;
  int first_missing_drawable = -1;
  for (int d = 1; d <= 24; ++d)
  {
    const int cx = static_cast<int>(std::floor((pos.x + dir.x * d * CHUNK_SIZE) / CHUNK_SIZE));
    const int cz = static_cast<int>(std::floor((pos.z + dir.y * d * CHUNK_SIZE) / CHUNK_SIZE));
    bool resident = false, drawable = false;
    column_state(cx, cz, resident, drawable);
    if (!resident && first_missing_resident < 0)
    {
      first_missing_resident = d;
    }
    if (!drawable && first_missing_drawable < 0)
    {
      first_missing_drawable = d;
    }
  }
  int ring_total = 0, ring_resident = 0, ring_drawable = 0;
  constexpr int kRing = 8;
  for (int dx = -kRing; dx <= kRing; ++dx)
  {
    for (int dz = -kRing; dz <= kRing; ++dz)
    {
      bool resident = false, drawable = false;
      column_state(cam_cx + dx, cam_cz + dz, resident, drawable);
      ++ring_total;
      ring_resident += resident;
      ring_drawable += drawable;
    }
  }
  json line;
  line["t_wall_s"] = std::chrono::duration<double>(now.time_since_epoch()).count();
  line["pos"] = {pos.x, pos.y, pos.z};
  line["speed_bps"] = speed;
  line["ahead_first_missing_resident_chunks"] = first_missing_resident;
  line["ahead_first_missing_drawable_chunks"] = first_missing_drawable;
  line["ring8_resident_frac"] = ring_total ? double(ring_resident) / ring_total : 0.0;
  line["ring8_drawable_frac"] = ring_total ? double(ring_drawable) / ring_total : 0.0;
  line["frame_ms_mean"] = frame_n ? frame_ms_sum / frame_n : 0.0;
  line["frame_ms_max"] = frame_ms_max;
  line["dirty_count"] = mesh.GetDirtyCount();
  line["async_in_flight"] = mesh.GetAsyncInFlightCount();
  line["fog_horizon_min"] = fog_min < 1e8f ? fog_min : -1.0f;
  line["fog_horizon_max"] = fog_max > -1e8f ? fog_max : -1.0f;
  line["fog_horizon_tv"] = fog_tv;
  line["fog_horizon_max_rate"] = fog_max_rate;
  line["fog_horizon_max_accel"] = fog_max_accel;
  fog_tv = fog_max_rate = fog_max_accel = 0.0;
  fog_min = 1e9f;
  fog_max = -1e9f;
  line["stream_candidates"] = st_candidates;
  line["stream_ring_blocked"] = st_ring_blocked;
  line["stream_async_queued"] = st_async_queued;
  line["stream_loads"] = st_loads;
  line["stream_near_skipped"] = st_near_skipped;
  line["gen_pending_max"] = st_pending_max;
  line["gen_inflight_max"] = st_inflight_max;
  line["gen_ready_max"] = st_ready_max;
  st_candidates = st_ring_blocked = st_async_queued = st_loads =
      st_near_skipped = 0;
  st_pending_max = st_inflight_max = st_ready_max = 0;
  line["greedy_cache"] = mesh.GetGreedyCacheSize();
  file << line.dump() << '\n';
  file.flush();
  frame_ms_sum = 0.0;
  frame_ms_max = 0.0;
  frame_n = 0;
  last_emit = now;
}

} // namespace cutum
