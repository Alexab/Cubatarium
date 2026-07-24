#include "World/Diagnostics/BlockInspectDiagnostics.h"

#include "App/Core.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Raycast/BlockRaycast.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace
{

using json = nlohmann::json;

std::atomic<int> &SampleCounter()
{
  static std::atomic<int> counter{0};
  return counter;
}

const char *RenderStyleName(BlockRenderStyle style)
{
  switch (style)
  {
  case BlockRenderStyle::UCube:
    return "cube";
  case BlockRenderStyle::Fluid:
    return "fluid";
  case BlockRenderStyle::Cross:
    return "cross";
  case BlockRenderStyle::Cutout:
    return "cutout";
  }
  return "unknown";
}

std::string IsoTimestampUtc()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

json Vec3Json(const glm::ivec3 &v)
{
  return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

json Vec3Json(const glm::vec3 &v)
{
  return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

json NeighborJson(const UBlockWorld &block_world, const UBlockRegistry &registry,
                  glm::ivec3 offset, glm::ivec3 block_pos)
{
  const glm::ivec3 pos = block_pos + offset;
  const BlockId id = block_world.GetBlock(pos);
  return json{{"offset", Vec3Json(offset)},
              {"pos", Vec3Json(pos)},
              {"block_id", static_cast<int>(id)},
              {"type_name", registry.GetTypeNameById(id)}};
}

std::vector<std::string>
BuildSuspectedCauses(BlockRenderStyle render_style, bool has_greedy_mesh,
                     bool chunk_mesh_dirty, bool async_mesh_inflight,
                     uint64_t current_revision, uint64_t inflight_revision,
                     bool texture_map_has_entry, GLuint texture_gl_id, BlockId id)
{
  std::vector<std::string> causes;
  if (id == BLOCK_AIR)
  {
    causes.push_back("data_inconsistency");
    return causes;
  }
  if (render_style == BlockRenderStyle::UCube && !has_greedy_mesh)
  {
    causes.push_back("missing_chunk_mesh");
  }
  if (chunk_mesh_dirty)
  {
    causes.push_back("mesh_dirty_pending");
  }
  if (async_mesh_inflight && inflight_revision != current_revision)
  {
    causes.push_back("stale_async_mesh");
  }
  if ((render_style == BlockRenderStyle::UCube ||
       render_style == BlockRenderStyle::Cutout) &&
      (!texture_map_has_entry || texture_gl_id == 0))
  {
    causes.push_back("missing_texture");
  }
  if (render_style == BlockRenderStyle::Cross)
  {
    causes.push_back("cross_instance_path");
  }
  return causes;
}

bool AppendJsonLine(const std::filesystem::path &path, const json &record)
{
  std::ofstream out(path, std::ios::app);
  if (!out.is_open())
  {
    return false;
  }
  out << record.dump() << '\n';
  return out.good();
}

} // namespace

std::filesystem::path UBlockInspectDiagnostics::DefaultLogPath()
{
  return GetExecutableDirectory() / "block_inspect.jsonl";
}

bool UBlockInspectDiagnostics::ClearLog()
{
  SampleCounter().store(0, std::memory_order_relaxed);
  std::error_code ec;
  std::filesystem::remove(DefaultLogPath(), ec);
  return true;
}

int UBlockInspectDiagnostics::GetSampleCount()
{
  return SampleCounter().load(std::memory_order_relaxed);
}

int UBlockInspectDiagnostics::CaptureFromCrosshair(const UWorld &world,
                                                   UGeometryEngine *geometries)
{
  const auto camera = world.GetCurrentUserCamera();
  if (!camera)
  {
    return -1;
  }
  const auto hit =
      RaycastSolidBlocks(world.GetBlockWorld(), world.GetBlockRegistry(),
                       camera->GetPosition(), camera->GetFront());
  if (!hit)
  {
    return -1;
  }
  return CaptureAndAppend(world, *hit, geometries);
}

int UBlockInspectDiagnostics::CaptureAndAppend(const UWorld &world,
                                               const BlockRayHit &hit,
                                               UGeometryEngine *geometries)
{
  const glm::ivec3 block_pos = hit.blockPos;
  const UBlockWorld &block_world = world.GetBlockWorld();
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const BlockId block_id = block_world.GetBlock(block_pos);
  const BlockRenderStyle render_style = registry.GetRenderStyle(block_id);

  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(block_pos);
  const glm::ivec3 local_pos = UChunkManager::WorldToLocal(block_pos);
  const UWorldMeshService &mesh = world.GetMeshService();

  bool texture_map_has_entry = false;
  const GLuint texture_gl_id =
      geometries ? geometries->InspectBlockGpuTexture(block_id,
                                                     &texture_map_has_entry)
                 : 0;

  const bool has_greedy_mesh = mesh.HasGreedyMesh(chunk_coord);
  const bool chunk_mesh_dirty = mesh.IsChunkMeshDirty(chunk_coord);
  const uint64_t chunk_mesh_revision = mesh.GetChunkMeshRevision(chunk_coord);
  const bool async_mesh_inflight = mesh.HasInflightMeshBuild(chunk_coord);
  const uint64_t inflight_revision =
      mesh.GetInflightSourceRevision(chunk_coord);

  const bool crosshair_has_block = world.GetIsBlockIntersectionExists();
  const glm::ivec3 crosshair_pos = world.GetBreakBlockPos();
  const bool crosshair_matches =
      crosshair_has_block && crosshair_pos == block_pos;

  const auto camera = world.GetCurrentUserCamera();
  const glm::vec3 camera_pos = camera ? camera->GetPosition() : glm::vec3(0.0f);
  const glm::vec3 camera_front = camera ? camera->GetFront() : glm::vec3(0.0f);

  const int sample_index =
      SampleCounter().fetch_add(1, std::memory_order_relaxed) + 1;

  const std::vector<std::string> suspected_causes = BuildSuspectedCauses(
      render_style, has_greedy_mesh, chunk_mesh_dirty, async_mesh_inflight,
      chunk_mesh_revision, inflight_revision, texture_map_has_entry,
      texture_gl_id, block_id);

  json record;
  record["schema"] = "block_inspect.v1";
  record["sample_index"] = sample_index;
  record["timestamp_utc"] = IsoTimestampUtc();
  record["world_name"] = world.GetWorldName();
  record["world_pos"] = Vec3Json(block_pos);
  record["block_id"] = static_cast<int>(block_id);
  record["type_name"] = registry.GetTypeNameById(block_id);
  record["render_style"] = RenderStyleName(render_style);
  record["is_transparent"] = registry.IsTransparent(block_id);
  record["blocks_movement"] = registry.BlocksMovement(block_id);
  record["is_solid"] = registry.IsSolid(block_id);
  record["neighbors"] = json::array(
      {NeighborJson(block_world, registry, glm::ivec3(1, 0, 0), block_pos),
       NeighborJson(block_world, registry, glm::ivec3(-1, 0, 0), block_pos),
       NeighborJson(block_world, registry, glm::ivec3(0, 1, 0), block_pos),
       NeighborJson(block_world, registry, glm::ivec3(0, -1, 0), block_pos),
       NeighborJson(block_world, registry, glm::ivec3(0, 0, 1), block_pos),
       NeighborJson(block_world, registry, glm::ivec3(0, 0, -1), block_pos)});
  record["raycast"] = json{{"camera_pos", Vec3Json(camera_pos)},
                           {"camera_front", Vec3Json(camera_front)},
                           {"hit_distance", hit.distance},
                           {"face_normal", Vec3Json(hit.faceNormal)},
                           {"crosshair_matches", crosshair_matches},
                           {"crosshair_pos", Vec3Json(crosshair_pos)}};
  record["chunk"] = json{{"chunk_coord", Vec3Json(chunk_coord)},
                         {"local_pos", Vec3Json(local_pos)},
                         {"chunk_loaded",
                          block_world.GetChunkManager().HasChunk(chunk_coord)}};
  record["mesh"] = json{
      {"has_greedy_mesh", has_greedy_mesh},
      {"chunk_mesh_dirty", chunk_mesh_dirty},
      {"chunk_mesh_revision", chunk_mesh_revision},
      {"async_mesh_inflight", async_mesh_inflight},
      {"inflight_source_revision", inflight_revision},
      {"greedy_cache_size", mesh.GetGreedyCacheSize()},
      {"dirty_chunks_pending", mesh.GetDirtyCount()},
      {"async_in_flight_total", mesh.GetAsyncInFlightCount()},
      {"global_mesh_revision", mesh.GetMeshRevision()}};
  record["texture"] = json{{"texture_map_has_entry", texture_map_has_entry},
                           {"texture_gl_id", texture_gl_id}};
  record["suspected_causes"] = suspected_causes;

  if (!AppendJsonLine(DefaultLogPath(), record))
  {
    return -1;
  }
  return sample_index;
}

} // namespace cutum
