#pragma once

#include "Blocks/BlockRegistry.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightResultInstall.h"
#include <array>
#include <chrono>

namespace cutum
{
/// Main-thread repair of previously calculated fields required by a paint.
/// Ordinary missing fields still use streaming/prefetch. Never invent AIR or
/// reuse an obsolete receipt. The soft time limit cannot interrupt one solve.
class SceneLightRefresh
{
public:
  enum class Status
  {
    Current,
    NoReceipt,
    Budget,
    Rejected,
    Refreshed
  };
  struct Result
  {
    Status status{Status::Rejected};
    bool changed{false};
    double elapsed_ms{0};
  };
  explicit SceneLightRefresh(unsigned max_attempts = 2, double soft_wall_ms = 4)
      : MaxAttempts(std::min(max_attempts, 2u)), SoftWallMs(soft_wall_ms)
  {
  }
  void BeginFrame(uint64_t world_epoch, uint64_t frame)
  {
    if (Epoch == world_epoch && Frame == frame)
      return;
    Epoch = world_epoch;
    Frame = frame;
    Attempts = 0;
    ElapsedMs = 0;
  }
  Result Refresh(UBlockWorld &world, const UBlockRegistry &registry,
                 glm::ivec3 coord)
  {
    const auto catalog = registry.GetDefinitionsCatalogSnapshot();
    if (!Epoch || !catalog)
      return {};
    if (world.HasCompleteLightSamplingCalculation(coord, catalog))
      return {Status::Current};
    if (!world.GetRecordedLightInputContext(coord, LightDomain::Sky) ||
        !world.GetRecordedLightInputContext(coord, LightDomain::Block))
      return {Status::NoReceipt};
    if (Attempts >= MaxAttempts || (SoftWallMs > 0 && ElapsedMs >= SoftWallMs))
      return {Status::Budget};
    for (unsigned n = 0; n < Attempts; ++n)
      if (Attempted[n] == coord)
        return {Status::Budget};
    const auto bounds =
        world.GetVerticalProfile().SliceBounds(coord.y, CHUNK_SIZE);
    if (!bounds)
      return {};
    Attempted[Attempts++] = coord;
    const auto started = std::chrono::steady_clock::now();
    RelightJobSpec spec;
    spec.min_world_y = bounds->first;
    spec.max_world_y = bounds->second;
    spec.include_known_air_outputs = true;
    auto snapshot =
        UChunkRelightSnapshot::CaptureSections(world, spec, {coord});
    snapshot.SetInputCatalog(catalog);
    const auto computed = snapshot.Compute(registry);
    Result result;
    if (computed.chunks.size() == 1 &&
        ValidateRelightInputs(computed, world,
                              registry.GetDefinitionsCatalogSnapshot(),
                              true) == RelightInputValidation::Current &&
        ComputedLightInteriorMatches(computed.chunks[0], true, true))
    {
      result.changed =
          InstallComputedLight(world, computed.chunks[0], true, true);
      if (world.RecordLightCalculation(computed, catalog) == 1 &&
          world.HasCompleteLightSamplingCalculation(coord, catalog))
        result.status = Status::Refreshed;
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    ElapsedMs += result.elapsed_ms;
    return result;
  }
  unsigned AttemptCount() const { return Attempts; }

private:
  uint64_t Epoch{0}, Frame{0};
  unsigned Attempts{0}, MaxAttempts{2};
  double ElapsedMs{0}, SoftWallMs{4};
  std::array<glm::ivec3, 2> Attempted{};
};
} // namespace cutum
