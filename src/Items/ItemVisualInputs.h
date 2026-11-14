#pragma once
namespace cutum
{
struct InventoryEntryRef;
/// Per-frame projection of existing gameplay state. No ownership or save data.
struct ItemVisualInputs
{
  float BowCharge{0.f}, UsePhase{0.f};
  int ContentsUnits{0};
  bool HasWater{false}, HasAmmo{false};
};
ItemVisualInputs MakeItemVisualInputs(const InventoryEntryRef *entry,
                                      float bowCharge = 0.f,
                                      float usePhase = 0.f,
                                      bool hasAmmo = false);
} // namespace cutum
