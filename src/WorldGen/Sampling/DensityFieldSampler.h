#pragma once

#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/DensityRouter.h"
#include <cstdint>
#include <unordered_map>

namespace cutum
{

struct DensityFieldParams
{
  float noiseScale{0.014f};
  int octaves{4};
  float persistence{0.5f};
  float lacunarity{2.0f};
  bool cavesInDensity{false};
  float caveAmplitude{18.0f};
  bool riversEnabled{false};
  float riverWidth{1.0f};
  float riverNoiseScale{0.008f};
  // Profile 3: bounded spline-like router (no raw block uplifts, 3D noise
  // scaled by a mountain/erosion factor).
  bool routerV3{false};
};

class UDensityFieldSampler
{
public:
  struct Column
  {
    int x{0};
    int z{0};
    int surfaceY{0};
    DensityRouteParams route{};
    float horizontalWobble{0.0f};
    float surfaceDensityOffset{0.0f};
  };

  UDensityFieldSampler(uint32_t seed, int seaLevel, int maxHeight,
                       float terrainRoughness,
                       const DensityFieldParams &params = DensityFieldParams{},
                       const CaveParams &caves = CaveParams{},
                       int minHeight = 0);

  float SampleTerrainDensity(int x, int y, int z, int octaves = -1) const;
  float SampleDensity(int x, int y, int z, int surface_y) const;
  Column PrepareColumn(int x, int z, int target_surface_y,
                       int raw_surface_y) const;
  float SampleDensity(const Column &column, int y) const;
  int SurfaceYAt(int x, int z) const;
  int CoarseSurfaceYAt(int x, int z) const;

  struct SamplingCacheStats
  {
    uint64_t RouteHits{0};
    uint64_t RouteMisses{0};
    uint64_t WobbleHits{0};
    uint64_t WobbleMisses{0};
    uint64_t OutOfRange{0};
  };
  void BeginChunkSamplingCache(int base_x, int base_z, int pad);
  void EndChunkSamplingCache();
  const SamplingCacheStats &GetSamplingCacheStats() const
  {
    return SamplingProfile;
  }

  /// Opt-in single-threaded diagnostics for GEN-1.1. Disabled profiling adds
  /// no timing or counter work to production density evaluation.
  struct ProfileStats
  {
    uint64_t surface_calls{0};
    uint64_t coarse_calls{0};
    uint64_t route_calls{0};
    uint64_t wobble_calls{0};
    uint64_t prepare_column_calls{0};
    uint64_t terrain_density_calls{0};
    uint64_t cave_density_calls{0};
    uint64_t scan_top_calls{0};
    double surface_ms{0.0};
    double coarse_ms{0.0};
    double route_ms{0.0};
    double wobble_ms{0.0};
    double terrain_density_ms{0.0};
  };
  void SetProfilingEnabled(bool enabled) { ProfilingEnabled = enabled; }
  void ResetProfileStats() { Profile = {}; }
  const ProfileStats &GetProfileStats() const { return Profile; }

private:
  float SampleTerrainDensityAt(int x, int y, int z, int octaves,
                               const DensityRouteParams &route,
                               float horizontal_wobble) const;
  float ApplyCaveDensityOffset(int x, int y, int z, int surface_y,
                               float density) const;
  DensityRouteParams RouteAt(int x, int z) const;
  DensityRouteParams ComputeRouteAt(int x, int z) const;
  int DensityScanTop(const DensityRouteParams &route, float horizontal_wobble,
                     int octaves) const;
  float HorizontalWobbleAt(int x, int z,
                           const DensityRouteParams &route) const;
  float ComputeHorizontalWobbleAt(int x, int z,
                                  const DensityRouteParams &route) const;

  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  int MinHeight;
  float TerrainRoughness;
  DensityFieldParams Params;
  CaveParams Caves;
  bool ProfilingEnabled{false};
  mutable ProfileStats Profile;
  bool SamplingCacheEnabled{false};
  int SamplingCacheOriginX{0};
  int SamplingCacheOriginZ{0};
  int SamplingCacheSize{0};
  mutable std::unordered_map<uint64_t, DensityRouteParams> RouteCache;
  mutable std::unordered_map<uint64_t, float> WobbleCache;
  mutable SamplingCacheStats SamplingProfile;
};

} // namespace cutum
