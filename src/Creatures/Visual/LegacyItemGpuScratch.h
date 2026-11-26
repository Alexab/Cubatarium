#pragma once

namespace cutum
{
struct BoneSkeletonCubeMeshCpu;
class TerrainGpuBufferCensus;
enum class TerrainGpuBufferRole;

// One reusable position/UV stream. Its renderer or shared mesh cache releases
// it while the owning GL context is current.
class LegacyItemGpuScratch
{
public:
  LegacyItemGpuScratch() = default;
  LegacyItemGpuScratch(const LegacyItemGpuScratch &) = delete;
  LegacyItemGpuScratch &operator=(const LegacyItemGpuScratch &) = delete;
  bool Draw(const BoneSkeletonCubeMeshCpu &mesh);
  void Reset() noexcept;
  void InspectGpuBuffers(TerrainGpuBufferCensus &out, TerrainGpuBufferRole vertices,
                         TerrainGpuBufferRole indices) const;

private:
  unsigned Vao{}, Vbo{}, Ebo{};
};
} // namespace cutum
