#pragma once

namespace cutum
{

struct FluidTuning
{
  static constexpr int WaterDropBoost = 4;
  static constexpr int FloodMaxPassesDefault = 8;
  static constexpr int CoastalPermeableBandAboveSea = 8;
  static constexpr int BreakSiteDecorScanDyMax = 6;
  static constexpr int BreakSiteSpillCellBudget = 30;
  static constexpr int DefaultWaterSpreadPeriod = 5;
  static constexpr int DefaultLavaSpreadPeriod = 30;
};

} // namespace cutum
