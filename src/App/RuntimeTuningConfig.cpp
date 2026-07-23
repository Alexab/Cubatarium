#include "App/RuntimeTuningConfig.h"

#include "World/Core/RuntimeTuning.h"
#include "World/Physics/FluidTuning.h"

#include <nlohmann/json.hpp>

namespace cutum
{

void ApplyRuntimeTuningFromConfig(const nlohmann::json *physics,
                                  const nlohmann::json *render,
                                  const nlohmann::json *procedural,
                                  const nlohmann::json *memory)
{
  URuntimeTuning &tuning = URuntimeTuning::Get();
  URuntimeTuning::ResetToDefaults();

  if (memory && memory->is_object())
  {
    if (memory->contains("tier") && (*memory)["tier"].is_string())
    {
      URuntimeTuning::ApplyMemoryTier(
          (*memory)["tier"].get_ref<const std::string &>().c_str());
    }
    tuning.MemoryBudgetMb =
        memory->value("budget_mb", tuning.MemoryBudgetMb);
    tuning.MemorySoftMb = memory->value("soft_mb", tuning.MemorySoftMb);
    tuning.MemoryExpandKeepMb =
        memory->value("expand_keep_mb", tuning.MemoryExpandKeepMb);
  }

  if (physics && physics->contains("fluid_tuning") &&
      (*physics)["fluid_tuning"].is_object())
  {
    const auto &fluid = (*physics)["fluid_tuning"];
    tuning.WaterDropBoost =
        fluid.value("water_drop_boost", FluidTuning::WaterDropBoost);
    tuning.FloodMaxPasses =
        fluid.value("flood_max_passes", FluidTuning::FloodMaxPassesDefault);
    tuning.CoastalBandAboveSea = fluid.value(
        "coastal_band_above_sea", FluidTuning::CoastalPermeableBandAboveSea);
  }

  if (render && render->contains("underwater_fog") &&
      (*render)["underwater_fog"].is_object())
  {
    const auto &fog = (*render)["underwater_fog"];
    tuning.FluidSurfaceScanUp = fog.value("surface_scan_up", 32);
    tuning.FluidSurfaceScanDown = fog.value("surface_scan_down", 64);
    tuning.FluidSurfaceWindowMoveThreshold =
        fog.value("surface_window_move_threshold", 16);
  }

  if (procedural && procedural->contains("tuning") &&
      (*procedural)["tuning"].is_object())
  {
    const auto &worldgen = (*procedural)["tuning"];
    tuning.HillsVegetationHeightNormMax =
        worldgen.value("hills_vegetation_height_norm_max", 0.82f);
  }
}

} // namespace cutum
