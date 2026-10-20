#include "World/Diagnostics/MeshAuditDiagnostics.h"

#include "App/Core.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"

#include <algorithm>
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
