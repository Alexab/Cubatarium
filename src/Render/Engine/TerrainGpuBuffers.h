#pragma once

#include "Render/GlIncludes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace cutum
{
enum class TerrainGpuBufferRole
{
  TerrainScratch, GreedyPool, GreedyBatch, GreedyDraw, GreedyCompute, CrossTemplate, CrossCurrent,
  CrossStaging, LightRowsCurrent, LightRowsAlternate, PackedPool, PackedCompute,
  PackedReadback, MdiScratch, OverlayQuad, GuiColored, GuiTextured, TextVertices,
  PreviewCubeVertices, PreviewCubeIndices, PreviewScratchVertices, PreviewScratchIndices,
  PreviewSurfaceVertices, PreviewSurfaceIndices, PreviewOverlay, PreviewViewmodelVertices,
  PreviewViewmodelIndices, InventoryIconReadback,
  CreaturePartVertices, CreaturePartIndices, CreatureSkeletalVertices, CreatureSkeletalIndices,
  CreatureSkinnedVertices, CreatureSkinnedIndices, CreaturePalette,
  WorldHeldSurfaceVertices, WorldHeldSurfaceIndices, ActorViewmodelVertices, ActorViewmodelIndices,
  ViewmodelSurfaceVertices, ViewmodelSurfaceIndices, EquipmentScratchVertices, EquipmentScratchIndices,
  ViewmodelScratchVertices, ViewmodelScratchIndices,
  GeometryCubeVertices, GeometryCubeIndices, GeometryCubeInstances,
  GeometryFaceVertices, GeometryFaceIndices, GeometryFaceInstances,
  GeometryPreviewVertices, GeometryPreviewIndices, GeometryOutlineVertices, GeometryOutlineIndices,
  GeometryCrosshairVertices, SkyGradient, BlockCracks, BlockDebris, InfluenceLines, WeatherParticles, WindowCapture, Count
};

inline constexpr std::array TerrainGpuBufferRoleNames{
    "terrain_scratch", "greedy_pool", "greedy_batch", "greedy_draw", "greedy_compute", "cross_template", "cross_current",
    "cross_staging", "light_rows_current", "light_rows_alternate", "packed_pool", "packed_compute",
    "packed_readback", "mdi_scratch", "overlay_quad", "gui_colored", "gui_textured", "text_vertices",
    "preview_cube_vertices", "preview_cube_indices", "preview_scratch_vertices", "preview_scratch_indices",
    "preview_surface_vertices", "preview_surface_indices", "preview_overlay", "preview_viewmodel_vertices",
    "preview_viewmodel_indices", "inventory_icon_readback",
    "creature_part_vertices", "creature_part_indices", "creature_skeletal_vertices", "creature_skeletal_indices",
    "creature_skinned_vertices", "creature_skinned_indices", "creature_palette",
    "world_held_surface_vertices", "world_held_surface_indices", "actor_viewmodel_vertices", "actor_viewmodel_indices",
    "viewmodel_surface_vertices", "viewmodel_surface_indices", "equipment_scratch_vertices", "equipment_scratch_indices",
    "viewmodel_scratch_vertices", "viewmodel_scratch_indices",
    "geometry_cube_vertices", "geometry_cube_indices", "geometry_cube_instances",
    "geometry_face_vertices", "geometry_face_indices", "geometry_face_instances",
    "geometry_preview_vertices", "geometry_preview_indices", "geometry_outline_vertices", "geometry_outline_indices",
    "geometry_crosshair_vertices", "sky_gradient", "block_cracks", "block_debris", "influence_lines", "weather_particles", "window_capture"};
static_assert(TerrainGpuBufferRoleNames.size() == static_cast<std::size_t>(TerrainGpuBufferRole::Count));
static_assert(TerrainGpuBufferRoleNames.size() <= std::numeric_limits<std::uint64_t>::digits);

struct TerrainGpuBufferSize
{
  std::size_t objects{};
  std::uint64_t bytes{};
};

struct TerrainGpuBufferSnapshot
{
  TerrainGpuBufferSize total;
  std::array<TerrainGpuBufferSize, TerrainGpuBufferRoleNames.size()> roles{};
  std::size_t references{}, namesWithoutObject{}, failedQueries{}, sharedAcrossRoles{};
  bool contextAvailable{}, overflow{};
};

// Collect and measure on the GL thread with the current context for this graph.
// Buffer names deduplicate aliases across slots, passes and application roots.
// The census owns no GL objects and measures the logical GL_BUFFER_SIZE.
class TerrainGpuBufferCensus
{
public:
  void Add(TerrainGpuBufferRole role, GLuint name)
  {
    if (!name)
      return;
    Names[name] |= std::uint64_t{1} << static_cast<std::size_t>(role);
    ++References;
  }

  void Include(const TerrainGpuBufferCensus &group)
  {
    for (const auto &[name, roles] : group.Names)
      Names[name] |= roles;
    References += group.References;
  }

  TerrainGpuBufferSnapshot Read() const
  {
    TerrainGpuBufferSnapshot result;
    result.references = References;
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    if (!glIsBuffer || !glBindBuffer || !glGetBufferParameteri64v)
      return result;
#endif
    if (!glGetString(GL_VERSION))
      return result;
    result.contextAvailable = true;
    if (Names.empty())
      return result;
    struct Binding
    {
      GLint original{};
      Binding() { glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &original); }
      ~Binding() { glBindBuffer(GL_COPY_READ_BUFFER, static_cast<GLuint>(original)); }
    } binding;
    for (const auto &[name, roles] : Names)
    {
      if (!glIsBuffer(name))
      {
        ++result.namesWithoutObject;
        continue;
      }
      glBindBuffer(GL_COPY_READ_BUFFER, name);
      GLint bound{};
      glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &bound);
      GLint64 size = -1;
      if (static_cast<GLuint>(bound) == name)
        glGetBufferParameteri64v(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &size);
      if (size < 0)
      {
        ++result.failedQueries;
        continue;
      }
      const auto bytes = static_cast<std::uint64_t>(size);
      if (bytes > std::numeric_limits<std::uint64_t>::max() - result.total.bytes)
      {
        result.overflow = true;
        continue;
      }
      ++result.total.objects;
      result.total.bytes += bytes;
      result.sharedAcrossRoles += (roles & (roles - 1)) != 0;
      for (std::size_t index = 0; index < result.roles.size(); ++index)
        if (roles & (std::uint64_t{1} << index))
        {
          ++result.roles[index].objects;
          result.roles[index].bytes += bytes;
        }
    }
    return result;
  }

private:
  std::unordered_map<GLuint, std::uint64_t> Names;
  std::size_t References{};
};
} // namespace cutum
