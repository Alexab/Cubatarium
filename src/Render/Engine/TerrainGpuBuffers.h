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
  PackedReadback, MdiScratch, OverlayQuad, Count
};

inline constexpr std::array TerrainGpuBufferRoleNames{
    "terrain_scratch", "greedy_pool", "greedy_batch", "greedy_draw", "greedy_compute", "cross_template", "cross_current",
    "cross_staging", "light_rows_current", "light_rows_alternate", "packed_pool", "packed_compute",
    "packed_readback", "mdi_scratch", "overlay_quad"};
static_assert(TerrainGpuBufferRoleNames.size() == static_cast<std::size_t>(TerrainGpuBufferRole::Count));
static_assert(TerrainGpuBufferRoleNames.size() <= std::numeric_limits<std::uint32_t>::digits);

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
    Names[name] |= std::uint32_t{1} << static_cast<std::size_t>(role);
    ++References;
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
        if (roles & (std::uint32_t{1} << index))
        {
          ++result.roles[index].objects;
          result.roles[index].bytes += bytes;
        }
    }
    return result;
  }

private:
  std::unordered_map<GLuint, std::uint32_t> Names;
  std::size_t References{};
};
} // namespace cutum
